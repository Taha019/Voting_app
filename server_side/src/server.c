/* ═══════════════════════════════════════════════════════════════════
 *  SONU Electronic Voting System — Assignment 3
 *  Iterative + Connectionless UDP Server  (POSIX / Linux)
 *
 *  Design:
 *    • UDP  (SOCK_DGRAM) — connectionless, no accept() loop.
 *    • After each recvfrom() the server fork()s a child process to
 *      handle the request and send the reply.  The parent immediately
 *      returns to recvfrom(), so it is never blocked by a slow client.
 *    • The server is therefore BOTH iterative (single listening socket,
 *      no accept) AND concurrent (one child per datagram).
 *    • SIGCHLD is set to SIG_IGN so zombies are reaped automatically.
 *
 *  Protocol:  pipe-delimited text commands / responses (unchanged).
 *
 *  Build:
 *    gcc -Wall -std=c99 -o bin/server src/server.c src/voter.c \
 *        src/candidate.c src/election.c src/file_io.c
 * ═══════════════════════════════════════════════════════════════════ */

/* Enables POSIX.1-2008 features such as getifaddrs() and fork().
 * Must be defined before any #include to take effect. */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>       /* printf, perror                          */
#include <stdlib.h>      /* exit, atoi                              */
#include <string.h>      /* memset, strncpy, strcmp, strtok         */
#include <unistd.h>      /* close, getpid                           */
#include <signal.h>      /* signal, SIG_IGN, SIGCHLD                */
#include <sys/wait.h>    /* waitpid (not called directly, but needed
                            for SIGCHLD-related types)              */
#include <sys/types.h>   /* pid_t, ssize_t                          */
#include <sys/socket.h>  /* socket, bind, sendto, recvfrom          */
#include <netinet/in.h>  /* sockaddr_in, INADDR_ANY, htons          */
#include <arpa/inet.h>   /* inet_ntoa                               */
#include <ifaddrs.h>     /* getifaddrs, freeifaddrs — list local IPs */

/* Application-layer headers shared between server and business logic */
#include "../headers/positions.h"  /* MAX_POSITIONS, POSITION_NAMES  */
#include "../headers/voter.h"      /* Voter struct, register/login   */
#include "../headers/candidate.h"  /* Candidate struct, register/login */
#include "../headers/election.h"   /* cast_vote(), generate_results() */

/* ── Configuration constants ─────────────────────────────────────── */
#define SERVER_PORT       8080   /* UDP port the server listens on    */
#define BUFFER_SIZE       4096   /* Max datagram payload size (bytes) */
#define MAX_FIELDS        128    /* Max pipe-delimited fields per msg */
#define MAX_CANDS_PER_POS 64     /* Max candidates per position       */


/* ═══════════════════════════════════════════════════════════════════
 *  print_local_ips()
 *
 *  Called once at startup to list every IPv4 address on this machine.
 *  This helps the user know which IP to give clients on the same LAN.
 *  For internet access the router's public IP is still needed separately.
 * ═══════════════════════════════════════════════════════════════════ */
static void print_local_ips(void)
{
    struct ifaddrs *ifap, *ifa;

    /* getifaddrs() builds a linked list of all network interfaces.
     * Returns 0 on success; bail out silently if it fails. */
    if (getifaddrs(&ifap) != 0) return;

    printf("[SERVER] Local interfaces:\n");

    /* Walk the linked list — ifa->ifa_next moves to the next interface */
    for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {

        /* Skip interfaces with no address, and skip non-IPv4 interfaces
         * (e.g. IPv6 entries have sa_family == AF_INET6) */
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;

        /* Cast the generic sockaddr pointer to the IPv4-specific type
         * so we can extract the IP address field */
        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;

        /* inet_ntoa() converts the binary IP to a human-readable string
         * e.g. 192.168.1.42 */
        printf("         %-12s  %s\n",
               ifa->ifa_name, inet_ntoa(sa->sin_addr));
    }

    /* Release the memory allocated by getifaddrs() */
    freeifaddrs(ifap);
    printf("\n");
}


/* ═══════════════════════════════════════════════════════════════════
 *  parse_fields()
 *
 *  Splits a pipe-delimited string in-place and stores pointers to
 *  each token in the fields[] array.
 *
 *  e.g. "LOGIN_VOTER|alice|pass123"
 *       → fields[0]="LOGIN_VOTER", fields[1]="alice", fields[2]="pass123"
 *       → returns 3
 *
 *  NOTE: strtok() modifies the string — always pass a writable copy.
 * ═══════════════════════════════════════════════════════════════════ */
static int parse_fields(char *str, char *fields[], int max_fields)
{
    int count = 0;

    /* strtok() returns a pointer to the first token, replacing the
     * delimiter '|' with '\0' so each token is a proper C string */
    char *token = strtok(str, "|");

    /* Subsequent calls pass NULL to continue from where strtok left off */
    while (token && count < max_fields) {
        fields[count++] = token;
        token = strtok(NULL, "|");
    }

    return count;  /* Total number of fields found */
}


/* ═══════════════════════════════════════════════════════════════════
 *  handle_command()
 *
 *  Receives the raw datagram payload, parses it, dispatches to the
 *  correct business-logic function, and writes the response string.
 *
 *  All responses follow the format:
 *    OK|...data...\n      on success
 *    ERR|REASON\n         on failure
 *
 *  This function runs inside the CHILD process, so it is safe to
 *  block on file I/O without affecting the parent's receive loop.
 * ═══════════════════════════════════════════════════════════════════ */
static void handle_command(const char *raw, char *resp, int resp_size)
{
    /* Copy the raw buffer into a local array because strtok() will
     * modify the string in-place — we must not alter the original */
    char cmd[BUFFER_SIZE];
    strncpy(cmd, raw, BUFFER_SIZE - 1);
    cmd[BUFFER_SIZE - 1] = '\0';  /* Guarantee null termination */

    /* Strip any trailing \r or \n (Windows clients may send \r\n) */
    cmd[strcspn(cmd, "\r\n")] = '\0';

    /* Parse the pipe-delimited fields into the f[] pointer array */
    char *f[MAX_FIELDS];
    int nf = parse_fields(cmd, f, MAX_FIELDS);

    /* Reject completely empty datagrams */
    if (nf == 0) {
        snprintf(resp, resp_size, "ERR|EMPTY_COMMAND\n");
        return;
    }

    /* ── REG_VOTER|name|user|pass ───────────────────────────────── *
     * Register a new voter. Returns the new voter_id on success,
     * or -1 if the username is already taken. */
    if (strcmp(f[0], "REG_VOTER") == 0) {
        if (nf < 4) { snprintf(resp, resp_size, "ERR|MISSING_FIELDS\n"); return; }

        int id = register_voter(f[1], f[2], f[3]);  /* name, user, pass */

        if (id > 0)
            snprintf(resp, resp_size, "OK|%d\n", id);
        else
            snprintf(resp, resp_size, "ERR|USERNAME_TAKEN\n");
    }

    /* ── REG_CAND|name|user|pass|pos_id ────────────────────────── *
     * Register a new candidate for a specific position.
     * Returns new candidate_id, -1 for duplicate username,
     * or -2 for an invalid position ID. */
    else if (strcmp(f[0], "REG_CAND") == 0) {
        if (nf < 5) { snprintf(resp, resp_size, "ERR|MISSING_FIELDS\n"); return; }

        int pos = atoi(f[4]);                              /* position ID (0–4) */
        int id  = register_candidate(f[1], f[2], f[3], pos);

        if (id > 0)
            snprintf(resp, resp_size, "OK|%d\n", id);
        else if (id == -2)
            snprintf(resp, resp_size, "ERR|INVALID_POSITION\n");
        else
            snprintf(resp, resp_size, "ERR|USERNAME_TAKEN\n");
    }

    /* ── LOGIN_VOTER|user|pass ──────────────────────────────────── *
     * Authenticate a voter. On success returns voter_id and full name
     * so the client can greet the user by name. */
    else if (strcmp(f[0], "LOGIN_VOTER") == 0) {
        if (nf < 3) { snprintf(resp, resp_size, "ERR|MISSING_FIELDS\n"); return; }

        int id = login_voter(f[1], f[2]);  /* scans voters.dat for match */

        if (id > 0) {
            Voter v;
            get_voter_by_id(id, &v);  /* fetch full record to get the name */
            snprintf(resp, resp_size, "OK|%d|%s\n", id, v.full_name);
        } else {
            snprintf(resp, resp_size, "ERR|INVALID_CREDENTIALS\n");
        }
    }

    /* ── LOGIN_CAND|user|pass ───────────────────────────────────── *
     * Authenticate a candidate. Returns id, name, position label,
     * and current vote count so the dashboard can display live stats. */
    else if (strcmp(f[0], "LOGIN_CAND") == 0) {
        if (nf < 3) { snprintf(resp, resp_size, "ERR|MISSING_FIELDS\n"); return; }

        int id = login_candidate(f[1], f[2]);

        if (id > 0) {
            Candidate c;
            get_candidate_by_id(id, &c);
            /* POSITION_NAMES[] maps position_id to a human-readable label */
            snprintf(resp, resp_size, "OK|%d|%s|%s|%d\n",
                     id, c.full_name,
                     POSITION_NAMES[c.position_id],
                     c.vote_count);
        } else {
            snprintf(resp, resp_size, "ERR|INVALID_CREDENTIALS\n");
        }
    }

    /* ── CAST_VOTE|voter_id|candidate_id|pos_id ─────────────────── *
     * Attempt to cast a vote. cast_vote() enforces two security checks
     * before writing anything: voter eligibility and candidate validity.
     * The switch maps each result code to the appropriate response. */
    else if (strcmp(f[0], "CAST_VOTE") == 0) {
        if (nf < 4) { snprintf(resp, resp_size, "ERR|MISSING_FIELDS\n"); return; }

        int voter_id = atoi(f[1]);
        int cand_id  = atoi(f[2]);
        int pos_id   = atoi(f[3]);

        int result = cast_vote(voter_id, cand_id, pos_id);

        switch (result) {
        case VOTE_OK:
            /* Both checks passed; vote was recorded successfully */
            snprintf(resp, resp_size, "OK|VOTE_CAST\n");
            break;
        case VOTE_ERR_ALREADY:
            /* Voter already cast a vote in this position's race */
            snprintf(resp, resp_size, "ERR|ALREADY_VOTED\n");
            break;
        case VOTE_ERR_INVALID:
            /* Candidate doesn't exist or belongs to a different position */
            snprintf(resp, resp_size, "ERR|INVALID_CANDIDATE\n");
            break;
        case VOTE_ERR_NOT_FOUND:
            /* Voter ID not found or voter is not registered */
            snprintf(resp, resp_size, "ERR|VOTER_NOT_FOUND\n");
            break;
        default:
            snprintf(resp, resp_size, "ERR|UNKNOWN\n");
        }
    }

    /* ── GET_RESULTS|pos_id ──────────────────────────────────────── *
     * Return all candidates for a position sorted by vote count (desc).
     * Response: OK|count|name1|votes1|name2|votes2|... */
    else if (strcmp(f[0], "GET_RESULTS") == 0) {
        if (nf < 2) { snprintf(resp, resp_size, "ERR|MISSING_FIELDS\n"); return; }

        int pos = atoi(f[1]);

        /* Validate the position ID before querying the file */
        if (pos < 0 || pos >= MAX_POSITIONS) {
            snprintf(resp, resp_size, "ERR|INVALID_POSITION\n");
            return;
        }

        Candidate results[MAX_CANDS_PER_POS];
        int count = generate_results(pos, results, MAX_CANDS_PER_POS);

        /* Build the response incrementally using an offset pointer.
         * snprintf returns the number of bytes written, so adding it
         * to offset moves the write position forward each iteration. */
        int offset = snprintf(resp, resp_size, "OK|%d", count);
        for (int i = 0; i < count && offset < resp_size - 100; i++)
            offset += snprintf(resp + offset, resp_size - offset,
                               "|%s|%d", results[i].full_name,
                               results[i].vote_count);

        strncat(resp, "\n", resp_size - strlen(resp) - 1);
    }

    /* ── LIST_CANDS|pos_id ───────────────────────────────────────── *
     * Return all registered candidates for a position (unsorted).
     * Includes candidate_id so the client can use it in CAST_VOTE.
     * Response: OK|count|id1|name1|votes1|id2|name2|votes2|... */
    else if (strcmp(f[0], "LIST_CANDS") == 0) {
        if (nf < 2) { snprintf(resp, resp_size, "ERR|MISSING_FIELDS\n"); return; }

        int pos = atoi(f[1]);

        if (pos < 0 || pos >= MAX_POSITIONS) {
            snprintf(resp, resp_size, "ERR|INVALID_POSITION\n");
            return;
        }

        Candidate arr[MAX_CANDS_PER_POS];
        int count  = get_candidates_for_position(pos, arr, MAX_CANDS_PER_POS);
        int offset = snprintf(resp, resp_size, "OK|%d", count);

        for (int i = 0; i < count && offset < resp_size - 128; i++)
            offset += snprintf(resp + offset, resp_size - offset,
                               "|%d|%s|%d",
                               arr[i].candidate_id,
                               arr[i].full_name,
                               arr[i].vote_count);

        strncat(resp, "\n", resp_size - strlen(resp) - 1);
    }

    /* ── VOTER_STATUS|voter_id ───────────────────────────────────── *
     * Return the voter's name and a flag for each position showing
     * whether they have already voted there (1) or not (0).
     * Response: OK|name|flag0|flag1|flag2|flag3|flag4 */
    else if (strcmp(f[0], "VOTER_STATUS") == 0) {
        if (nf < 2) { snprintf(resp, resp_size, "ERR|MISSING_FIELDS\n"); return; }

        int vid = atoi(f[1]);
        Voter v;

        /* get_voter_by_id() returns 0 if the record doesn't exist */
        if (!get_voter_by_id(vid, &v)) {
            snprintf(resp, resp_size, "ERR|NOT_FOUND\n");
            return;
        }

        int offset = snprintf(resp, resp_size, "OK|%s", v.full_name);

        /* Append one flag per position: 0 = not voted, 1 = voted */
        for (int i = 0; i < MAX_POSITIONS; i++)
            offset += snprintf(resp + offset, resp_size - offset,
                               "|%d", v.voted_positions[i]);

        strncat(resp, "\n", resp_size - strlen(resp) - 1);
    }

    /* ── PING ────────────────────────────────────────────────────── *
     * Simple health-check command. Useful to verify the server is
     * alive and reachable before running the full client. */
    else if (strcmp(f[0], "PING") == 0) {
        snprintf(resp, resp_size, "OK|PONG\n");
    }

    /* ── Unknown command ─────────────────────────────────────────── */
    else {
        snprintf(resp, resp_size, "ERR|UNKNOWN_COMMAND\n");
    }
}


/* ═══════════════════════════════════════════════════════════════════
 *  main()
 *
 *  Server entry point. Sets up the UDP socket, binds it, then enters
 *  the main receive loop. Each arriving datagram causes a fork() —
 *  the child handles the request while the parent loops immediately.
 * ═══════════════════════════════════════════════════════════════════ */
int main(void)
{
    /* ── Prevent zombie child processes ──────────────────────────── *
     * When a child exits it sends SIGCHLD to the parent. Normally the
     * parent must call wait() to clean it up. Setting SIGCHLD to SIG_IGN
     * tells the kernel to reap children automatically the moment they
     * exit, so the process table never fills up with dead processes. */
    signal(SIGCHLD, SIG_IGN);

    /* ── Create the UDP socket ───────────────────────────────────── *
     * AF_INET    = IPv4
     * SOCK_DGRAM = UDP (connectionless datagrams, no handshake)
     * 0          = let the OS pick the protocol (UDP for SOCK_DGRAM) */
    int server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sock < 0) {
        perror("[SERVER] socket() failed");
        return 1;
    }

    /* ── Allow immediate port reuse after a restart ──────────────── *
     * Without SO_REUSEADDR, if the server crashes and restarts quickly,
     * bind() fails with "Address already in use" because the port is
     * still in TIME_WAIT. This option bypasses that wait. */
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* ── Configure the server address ───────────────────────────── */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); /* Zero all fields first */
    server_addr.sin_family      = AF_INET;         /* IPv4                 */
    server_addr.sin_addr.s_addr = INADDR_ANY;      /* Accept on all interfaces
                                                      (localhost, LAN, Tailscale) */
    server_addr.sin_port        = htons(SERVER_PORT); /* htons converts port from
                                                         host byte order to network
                                                         byte order (big-endian)  */

    /* ── Bind the socket to the address and port ─────────────────── *
     * After bind(), the OS will deliver any UDP datagram arriving on
     * port 8080 (on any interface) to this socket. */
    if (bind(server_sock,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("[SERVER] bind() failed");
        close(server_sock);
        return 1;
    }

    /* Startup banner */
    printf("==========================================\n");
    printf("    SONU VOTING SYSTEM  —  SERVER v3.0   \n");
    printf("    UDP Connectionless + fork() per req  \n");
    printf("    Listening on 0.0.0.0:%d             \n", SERVER_PORT);
    printf("==========================================\n\n");

    print_local_ips();  /* Show all IPs clients can connect to */

    printf("[SERVER] Awaiting UDP datagrams...\n\n");

    /* ── Declare buffers and the client address struct ───────────── *
     * client_addr is filled by recvfrom() with the sender's IP + port.
     * We need it to know where to send the reply back to. */
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    /* ═══════════════════════════════════════════════════════════════
     *  MAIN RECEIVE LOOP  (iterative)
     *
     *  The parent process loops here forever. Each iteration:
     *    1. Blocks on recvfrom() until a datagram arrives.
     *    2. fork()s a child to handle it.
     *    3. Returns to recvfrom() immediately — no waiting for child.
     *
     *  This makes the server iterative (one listening point, one loop)
     *  but still able to serve multiple clients at the same time because
     *  each child runs independently in parallel.
     * ═══════════════════════════════════════════════════════════════ */
    while (1) {

        /* Clear buffers before each receive to avoid stale data */
        memset(buffer, 0, BUFFER_SIZE);
        memset(&client_addr, 0, sizeof(client_addr));

        /* ── recvfrom() — the heart of the iterative loop ───────── *
         * Blocks here until a UDP datagram arrives on port 8080.
         * On return:
         *   buffer[]    — contains the raw datagram payload
         *   client_addr — contains the sender's IP address and port
         *   bytes_recv  — number of bytes received (-1 on error) */
        ssize_t bytes_recv = recvfrom(server_sock,
                                      buffer, BUFFER_SIZE - 1, 0,
                                      (struct sockaddr *)&client_addr,
                                      &addr_len);

        if (bytes_recv < 0) {
            /* Non-fatal error — log it and wait for the next datagram */
            perror("[SERVER] recvfrom() failed");
            continue;
        }

        /* Null-terminate the received data so we can use string functions */
        buffer[bytes_recv] = '\0';

        /* Log the incoming request with the sender's IP and port */
        printf("[SERVER] Datagram from %s:%d  ->  %s",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               buffer);

        /* ── fork() — hand off this request to a child process ──── *
         * fork() duplicates the entire process.
         * Return value:
         *   0        → we are in the CHILD
         *   positive → we are in the PARENT (value is child's PID)
         *   -1       → fork failed */
        pid_t pid = fork();

        if (pid < 0) {
            /* fork() failed — system is likely out of process slots.
             * Send an error to the client and keep the server running. */
            perror("[SERVER] fork() failed");
            const char *err_msg = "ERR|FORK_FAILED\n";
            sendto(server_sock, err_msg, strlen(err_msg), 0,
                   (struct sockaddr *)&client_addr, addr_len);
            continue;
        }

        if (pid == 0) {
            /* ════════════════════════════════════════════════════════
             *  CHILD PROCESS
             *
             *  The child inherits a copy of:
             *    - server_sock (the open UDP socket)
             *    - buffer[]    (the received datagram)
             *    - client_addr (who sent it)
             *
             *  It handles the command, sends the reply, then exits.
             *  The parent is NOT waiting — it has already looped back
             *  to recvfrom() by the time the child finishes its work.
             * ════════════════════════════════════════════════════════ */
            printf("[CHILD %d] Processing: %s", getpid(), buffer);

            /* Clear the response buffer before building the reply */
            memset(response, 0, BUFFER_SIZE);

            /* Dispatch the command and write the response string */
            handle_command(buffer, response, BUFFER_SIZE);

            printf("[CHILD %d] Sending  : %s", getpid(), response);

            /* Send the response datagram back to the original sender.
             * client_addr contains the IP and port from recvfrom() above. */
            if (sendto(server_sock,
                       response, strlen(response), 0,
                       (struct sockaddr *)&client_addr, addr_len) < 0) {
                perror("[CHILD] sendto() failed");
            }

            /* The child closes its inherited copy of the socket.
             * The parent's copy remains open — it still owns the listener. */
            close(server_sock);

            /* Child's work is done — exit cleanly.
             * Because SIGCHLD = SIG_IGN, the kernel cleans up this
             * process automatically with no zombie left behind. */
            exit(0);
        }

        /* ── PARENT resumes here immediately after fork() ─────────── *
         * The parent does NOT call wait() — that would block it until
         * the child finishes, defeating the purpose of forking.
         * SIGCHLD = SIG_IGN handles cleanup in the background. */
        printf("[SERVER] Forked child PID %d — back to listening.\n\n", pid);

        /* Loop back to recvfrom() — parent is always ready for the next
         * datagram, regardless of how long the child takes to respond. */
    }

    /* This line is never reached in normal operation.
     * The server runs until killed with Ctrl-C (SIGINT). */
    close(server_sock);
    return 0;
}