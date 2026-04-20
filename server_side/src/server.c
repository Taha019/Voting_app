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

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>           /* getifaddrs — prints local IPs at startup */

#include "../headers/positions.h"
#include "../headers/voter.h"
#include "../headers/candidate.h"
#include "../headers/election.h"

/* ── Configuration ───────────────────────────────────────────────── */
#define SERVER_PORT       8080
#define BUFFER_SIZE       4096
#define MAX_FIELDS        128
#define MAX_CANDS_PER_POS 64

/* ── Print all local IPv4 addresses ─────────────────────────────── *
 * Called once at startup so the user knows which IPs to give to
 * clients on the same network.  For cross-network access they still
 * need the router's public IP (curl ifconfig.me).
 */
static void print_local_ips(void)
{
    struct ifaddrs *ifap, *ifa;
    if (getifaddrs(&ifap) != 0) return;

    printf("[SERVER] Local interfaces:\n");
    for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;
        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        printf("         %-12s  %s\n",
               ifa->ifa_name, inet_ntoa(sa->sin_addr));
    }
    freeifaddrs(ifap);
    printf("\n");
}

/* ── Pipe-delimited field parser ─────────────────────────────────── */
static int parse_fields(char *str, char *fields[], int max_fields)
{
    int count = 0;
    char *token = strtok(str, "|");
    while (token && count < max_fields) {
        fields[count++] = token;
        token = strtok(NULL, "|");
    }
    return count;
}

/* ── Command dispatcher ──────────────────────────────────────────── *
 * Identical business-logic dispatch to Assignment 2.
 * Reads the raw command, executes the appropriate handler, and writes
 * the pipe-delimited response into resp.
 */
static void handle_command(const char *raw, char *resp, int resp_size)
{
    char cmd[BUFFER_SIZE];
    strncpy(cmd, raw, BUFFER_SIZE - 1);
    cmd[BUFFER_SIZE - 1] = '\0';
    cmd[strcspn(cmd, "\r\n")] = '\0';      /* Strip trailing newline */

    char *f[MAX_FIELDS];
    int nf = parse_fields(cmd, f, MAX_FIELDS);

    if (nf == 0) {
        snprintf(resp, resp_size, "ERR|EMPTY_COMMAND\n");
        return;
    }

    /* ── REG_VOTER|name|user|pass ───────────────────────────────── */
    if (strcmp(f[0], "REG_VOTER") == 0) {
        if (nf < 4) { snprintf(resp, resp_size, "ERR|MISSING_FIELDS\n"); return; }
        int id = register_voter(f[1], f[2], f[3]);
        if (id > 0)
            snprintf(resp, resp_size, "OK|%d\n", id);
        else
            snprintf(resp, resp_size, "ERR|USERNAME_TAKEN\n");
    }
    /* ── REG_CAND|name|user|pass|pos_id ────────────────────────── */
    else if (strcmp(f[0], "REG_CAND") == 0) {
        if (nf < 5) { snprintf(resp, resp_size, "ERR|MISSING_FIELDS\n"); return; }
        int pos = atoi(f[4]);
        int id  = register_candidate(f[1], f[2], f[3], pos);
        if (id > 0)
            snprintf(resp, resp_size, "OK|%d\n", id);
        else if (id == -2)
            snprintf(resp, resp_size, "ERR|INVALID_POSITION\n");
        else
            snprintf(resp, resp_size, "ERR|USERNAME_TAKEN\n");
    }
    /* ── LOGIN_VOTER|user|pass ──────────────────────────────────── */
    else if (strcmp(f[0], "LOGIN_VOTER") == 0) {
        if (nf < 3) { snprintf(resp, resp_size, "ERR|MISSING_FIELDS\n"); return; }
        int id = login_voter(f[1], f[2]);
        if (id > 0) {
            Voter v;
            get_voter_by_id(id, &v);
            snprintf(resp, resp_size, "OK|%d|%s\n", id, v.full_name);
        } else {
            snprintf(resp, resp_size, "ERR|INVALID_CREDENTIALS\n");
        }
    }
    /* ── LOGIN_CAND|user|pass ───────────────────────────────────── */
    else if (strcmp(f[0], "LOGIN_CAND") == 0) {
        if (nf < 3) { snprintf(resp, resp_size, "ERR|MISSING_FIELDS\n"); return; }
        int id = login_candidate(f[1], f[2]);
        if (id > 0) {
            Candidate c;
            get_candidate_by_id(id, &c);
            snprintf(resp, resp_size, "OK|%d|%s|%s|%d\n",
                     id, c.full_name,
                     POSITION_NAMES[c.position_id],
                     c.vote_count);
        } else {
            snprintf(resp, resp_size, "ERR|INVALID_CREDENTIALS\n");
        }
    }
    /* ── CAST_VOTE|voter_id|candidate_id|pos_id ─────────────────── */
    else if (strcmp(f[0], "CAST_VOTE") == 0) {
        if (nf < 4) { snprintf(resp, resp_size, "ERR|MISSING_FIELDS\n"); return; }
        int voter_id = atoi(f[1]);
        int cand_id  = atoi(f[2]);
        int pos_id   = atoi(f[3]);
        int result   = cast_vote(voter_id, cand_id, pos_id);
        switch (result) {
        case VOTE_OK:            snprintf(resp, resp_size, "OK|VOTE_CAST\n");          break;
        case VOTE_ERR_ALREADY:   snprintf(resp, resp_size, "ERR|ALREADY_VOTED\n");     break;
        case VOTE_ERR_INVALID:   snprintf(resp, resp_size, "ERR|INVALID_CANDIDATE\n"); break;
        case VOTE_ERR_NOT_FOUND: snprintf(resp, resp_size, "ERR|VOTER_NOT_FOUND\n");   break;
        default:                 snprintf(resp, resp_size, "ERR|UNKNOWN\n");
        }
    }
    /* ── GET_RESULTS|pos_id ──────────────────────────────────────── */
    else if (strcmp(f[0], "GET_RESULTS") == 0) {
        if (nf < 2) { snprintf(resp, resp_size, "ERR|MISSING_FIELDS\n"); return; }
        int pos = atoi(f[1]);
        if (pos < 0 || pos >= MAX_POSITIONS) {
            snprintf(resp, resp_size, "ERR|INVALID_POSITION\n");
            return;
        }
        Candidate results[MAX_CANDS_PER_POS];
        int count  = generate_results(pos, results, MAX_CANDS_PER_POS);
        int offset = snprintf(resp, resp_size, "OK|%d", count);
        for (int i = 0; i < count && offset < resp_size - 100; i++)
            offset += snprintf(resp + offset, resp_size - offset,
                               "|%s|%d", results[i].full_name,
                               results[i].vote_count);
        strncat(resp, "\n", resp_size - strlen(resp) - 1);
    }
    /* ── LIST_CANDS|pos_id ───────────────────────────────────────── */
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
    /* ── VOTER_STATUS|voter_id ───────────────────────────────────── */
    else if (strcmp(f[0], "VOTER_STATUS") == 0) {
        if (nf < 2) { snprintf(resp, resp_size, "ERR|MISSING_FIELDS\n"); return; }
        int vid = atoi(f[1]);
        Voter v;
        if (!get_voter_by_id(vid, &v)) {
            snprintf(resp, resp_size, "ERR|NOT_FOUND\n");
            return;
        }
        int offset = snprintf(resp, resp_size, "OK|%s", v.full_name);
        for (int i = 0; i < MAX_POSITIONS; i++)
            offset += snprintf(resp + offset, resp_size - offset,
                               "|%d", v.voted_positions[i]);
        strncat(resp, "\n", resp_size - strlen(resp) - 1);
    }

    else if (strcmp(f[0], "PING") == 0) {
    snprintf(resp, resp_size, "OK|PONG\n");
    }
    /* ── Unknown command ─────────────────────────────────────────── */
    else {
        snprintf(resp, resp_size, "ERR|UNKNOWN_COMMAND\n");
    }
}

/* ── Entry point ─────────────────────────────────────────────────── */

int main(void)
{
    /* ── Auto-reap child processes — no zombie accumulation ──────── */
    signal(SIGCHLD, SIG_IGN);

    /* ── Create UDP (connectionless) socket ──────────────────────── */
    int server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sock < 0) {
        perror("[SERVER] socket() failed");
        return 1;
    }

    /* Allow immediate port reuse after a restart */
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* ── Bind to all interfaces ───────────────────────────────────── */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(SERVER_PORT);

    if (bind(server_sock,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("[SERVER] bind() failed");
        close(server_sock);
        return 1;
    }

    printf("==========================================\n");
    printf("    SONU VOTING SYSTEM  —  SERVER v3.0   \n");
    printf("    UDP Connectionless + fork() per req  \n");
    printf("    Listening on 0.0.0.0:%d             \n", SERVER_PORT);
    printf("==========================================\n\n");

    /* Print every local IPv4 address so the user knows what to give clients */
    print_local_ips();

    printf("[SERVER] Awaiting UDP datagrams...\n\n");

    /* ── Main receive loop (iterative) ──────────────────────────── *
     * recvfrom() blocks until a datagram arrives.
     * The parent fork()s a child to handle each datagram, then
     * immediately loops back to recvfrom() — never blocking on I/O.
     * Because SIGCHLD = SIG_IGN the kernel reaps children automatically.
     */
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        memset(&client_addr, 0, sizeof(client_addr));

        /* Block until a datagram arrives */
        ssize_t bytes_recv = recvfrom(server_sock,
                                      buffer, BUFFER_SIZE - 1, 0,
                                      (struct sockaddr *)&client_addr,
                                      &addr_len);
        if (bytes_recv < 0) {
            perror("[SERVER] recvfrom() failed");
            continue;
        }

        buffer[bytes_recv] = '\0';

        /* Log incoming request in the parent before forking */
        printf("[SERVER] Datagram from %s:%d  ->  %s",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               buffer);

        /* ── fork() — spawn a child to handle this one request ───── */
        pid_t pid = fork();

        if (pid < 0) {
            /* fork failed: send an error and keep going */
            perror("[SERVER] fork() failed");
            const char *err_msg = "ERR|FORK_FAILED\n";
            sendto(server_sock, err_msg, strlen(err_msg), 0,
                   (struct sockaddr *)&client_addr, addr_len);
            continue;
        }

        if (pid == 0) {
            /* ────────────────────────────────────────────────────── *
             * CHILD PROCESS
             * Inherits the open server_sock and the datagram content.
             * Processes the command, sends the reply, then exits.
             * ────────────────────────────────────────────────────── */
            printf("[CHILD %d] Processing: %s", getpid(), buffer);

            memset(response, 0, BUFFER_SIZE);
            handle_command(buffer, response, BUFFER_SIZE);

            printf("[CHILD %d] Sending  : %s", getpid(), response);

            if (sendto(server_sock,
                       response, strlen(response), 0,
                       (struct sockaddr *)&client_addr, addr_len) < 0) {
                perror("[CHILD] sendto() failed");
            }

            close(server_sock);   /* Child closes its inherited copy  */
            exit(0);              /* Child done — parent still looping */
        }

        /* ── PARENT resumes here immediately after fork() ─────────── */
        printf("[SERVER] Forked child PID %d — back to listening.\n\n", pid);
        /* Parent does NOT close server_sock; it owns the listening socket */
    }

    /* Unreachable in normal operation — Ctrl-C terminates the server */
    close(server_sock);
    return 0;
}