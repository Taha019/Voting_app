/* ═══════════════════════════════════════════════════════════════════
 * SONU Electronic Voting System — Assignment 3
 * UDP Client (Single File Version - Fully Integrated)
 * ═══════════════════════════════════════════════════════════════════ */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Platform-Specific Networking ────────────────────────────────── */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    #define CLOSE_SOCK(s)  closesocket(s)
    #define SOCK_ERR       INVALID_SOCKET
    #define BAD_SOCK(s)    ((s) == INVALID_SOCKET)
    #define RECV_FAILED(n) ((n) == SOCKET_ERROR)
    typedef SOCKET sock_t;
    static void set_recv_timeout(SOCKET s, int seconds) {
        DWORD ms = (DWORD)(seconds * 1000);
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof(ms));
    }
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <sys/time.h>
    #define CLOSE_SOCK(s)  close(s)
    #define SOCK_ERR       (-1)
    #define BAD_SOCK(s)    ((s) < 0)
    #define RECV_FAILED(n) ((n) < 0)
    typedef int sock_t;
    static void set_recv_timeout(int s, int seconds) {
        struct timeval tv; tv.tv_sec = seconds; tv.tv_usec = 0;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
#endif

/* ── Positions.h Integrated Logic ────────────────────────────────── */
#define MAX_POSITIONS     5
#define MAX_POSITION_NAME 32
#define MAX_NAME_LEN      64
#define MAX_PASS_LEN      32

static const char * const POSITION_NAMES[MAX_POSITIONS] = {
    "Chairman", "Vice Chairman", "Secretary", "Treasurer", "PRO"
};

/* ── Global Settings ─────────────────────────────────────────────── */
#define BUFFER_SIZE  4096
#define MAX_FIELDS   128
#define RECV_TIMEOUT 5

static char g_server_ip[128] = "147.185.221.223"; 
static int  g_server_port   = 3467;

/* ── Network Engine ──────────────────────────────────────────────── */
static int send_command(const char *command, char *response, int resp_size)
{
    sock_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (BAD_SOCK(sock)) return 0;

    set_recv_timeout(sock, RECV_TIMEOUT);

    struct hostent *he = gethostbyname(g_server_ip);
    if (he == NULL) {
        CLOSE_SOCK(sock);
        return 0;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(g_server_port);
    memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);

    sendto(sock, command, (int)strlen(command), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

    memset(response, 0, resp_size);
    int bytes = recvfrom(sock, response, resp_size - 1, 0, NULL, NULL);
    CLOSE_SOCK(sock);

    if (RECV_FAILED(bytes)) {
        printf("  [!] No response from server (timeout after %ds).\n", RECV_TIMEOUT);
        return 0;
    }

    response[bytes] = '\0';
    return 1;
}

/* ── UI Helpers ──────────────────────────────────────────────────── */
static void read_line(const char *prompt, char *buf, int size) {
    printf("%s", prompt); fflush(stdout);
    if (fgets(buf, size, stdin) == NULL) { buf[0] = '\0'; return; }
    buf[strcspn(buf, "\r\n")] = '\0';
}
static int read_int(const char *prompt) {
    char buf[32]; read_line(prompt, buf, sizeof(buf)); return atoi(buf);
}
static int parse_response(char *resp, char *fields[], int max_fields) {
    int count = 0;
    char *token = strtok(resp, "|");
    while (token && count < max_fields) {
        fields[count++] = token;
        token = strtok(NULL, "|");
    }
    return count;
}
static int choose_position(void) {
    printf("\n  Positions:\n");
    for (int i = 0; i < MAX_POSITIONS; i++) printf("    %d. %s\n", i, POSITION_NAMES[i]);
    int pos = read_int("  Enter Position ID (0-4): ");
    return (pos < 0 || pos >= MAX_POSITIONS) ? -1 : pos;
}

/* ── Portals ─────────────────────────────────────────────────────── */
static void voting_booth(int voter_id, const char *voter_name) {
    int choice; char cmd[BUFFER_SIZE], resp[BUFFER_SIZE], *fields[MAX_FIELDS];
    do {
        printf("\n=== VOTING BOOTH (%s) ===\n", voter_name);
        printf("  1. List Candidates\n  2. Cast Vote\n  3. My Status\n  4. Logout\n");
        choice = read_int("  Choice: ");
        switch (choice) {
            case 1: {
                int pos = choose_position(); if (pos < 0) break;
                sprintf(cmd, "LIST_CANDS|%d\n", pos);
                if (send_command(cmd, resp, sizeof(resp))) {
                    char r_copy[BUFFER_SIZE]; strcpy(r_copy, resp);
                    int nf = parse_response(r_copy, fields, MAX_FIELDS);
                    if (strcmp(fields[0], "OK") == 0) {
                        int count = atoi(fields[1]);
                        printf("\n  Candidates for %s:\n", POSITION_NAMES[pos]);
                        for(int i=0; i<count; i++) printf("  ID: %s | %-20s (%s votes)\n", fields[2+i*3], fields[3+i*3], fields[4+i*3]);
                    }
                }
                break;
            }
            case 2: {
                int pos = choose_position(); if (pos < 0) break;
                int cid = read_int("  Enter Candidate ID: ");
                sprintf(cmd, "CAST_VOTE|%d|%d|%d\n", voter_id, cid, pos);
                if (send_command(cmd, resp, sizeof(resp))) printf("  Server response: %s\n", resp);
                break;
            }
            case 3: {
                sprintf(cmd, "VOTER_STATUS|%d\n", voter_id);
                if (send_command(cmd, resp, sizeof(resp))) {
                    char r_copy[BUFFER_SIZE]; strcpy(r_copy, resp);
                    int nf = parse_response(r_copy, fields, MAX_FIELDS);
                    if (strcmp(fields[0], "OK") == 0) {
                        for(int i=0; i<MAX_POSITIONS; i++) printf("  %-15s: %s\n", POSITION_NAMES[i], (2+i < nf && atoi(fields[2+i])) ? "[VOTED]" : "[PENDING]");
                    }
                }
                break;
            }
        }
    } while (choice != 4);
}

static void voter_portal(void) {
    int choice; char cmd[BUFFER_SIZE], resp[BUFFER_SIZE], *fields[16];
    do {
        printf("\n=== VOTER PORTAL ===\n  1. Register\n  2. Login\n  3. Back\n");
        choice = read_int("  Choice: ");
        if (choice == 1) {
            char n[64], u[64], p[64];
            read_line("  Full Name: ", n, 64); read_line("  Username: ", u, 64); read_line("  Password: ", p, 64);
            sprintf(cmd, "REG_VOTER|%s|%s|%s\n", n, u, p);
            if (send_command(cmd, resp, sizeof(resp))) printf("  Server: %s\n", resp);
        } else if (choice == 2) {
            char u[64], p[64];
            read_line("  Username: ", u, 64); read_line("  Password: ", p, 64);
            sprintf(cmd, "LOGIN_VOTER|%s|%s\n", u, p);
            if (send_command(cmd, resp, sizeof(resp))) {
                char r_copy[BUFFER_SIZE]; strcpy(r_copy, resp);
                int nf = parse_response(r_copy, fields, 16);
                if (strcmp(fields[0], "OK") == 0) voting_booth(atoi(fields[1]), fields[2]);
                else printf("  [ERR] Login failed\n");
            }
        }
    } while (choice != 3);
}

static void candidate_portal(void) {
    int choice; char cmd[BUFFER_SIZE], resp[BUFFER_SIZE], *fields[16];
    do {
        printf("\n=== CANDIDATE PORTAL ===\n  1. Register\n  2. Login\n  3. Back\n");
        choice = read_int("  Choice: ");
        if (choice == 1) {
            char n[64], u[64], p[64];
            read_line("  Full Name: ", n, 64); read_line("  Username: ", u, 64); read_line("  Password: ", p, 64);
            int pos = choose_position();
            sprintf(cmd, "REG_CAND|%s|%s|%s|%d\n", n, u, p, pos);
            if (send_command(cmd, resp, sizeof(resp))) printf("  Server: %s\n", resp);
        } else if (choice == 2) {
            char u[64], p[64];
            read_line("  Username: ", u, 64); read_line("  Password: ", p, 64);
            sprintf(cmd, "LOGIN_CAND|%s|%s\n", u, p);
            if (send_command(cmd, resp, sizeof(resp))) {
                char r_copy[BUFFER_SIZE]; strcpy(r_copy, resp);
                int nf = parse_response(r_copy, fields, 16);
                if (strcmp(fields[0], "OK") == 0) printf("\n  ID: %s | Name: %s\n  Pos: %s | Votes: %s\n", fields[1], fields[2], fields[3], fields[4]);
                else printf("  [ERR] Login failed\n");
            }
        }
    } while (choice != 3);
}

static void results_menu(void) {
    char cmd[BUFFER_SIZE], resp[BUFFER_SIZE], *fields[MAX_FIELDS];
    printf("\n=== ELECTION RESULTS ===\n");
    for (int i = 0; i < MAX_POSITIONS; i++) {
        sprintf(cmd, "GET_RESULTS|%d\n", i);
        if (send_command(cmd, resp, sizeof(resp))) {
            char r_copy[BUFFER_SIZE]; strcpy(r_copy, resp);
            int nf = parse_response(r_copy, fields, MAX_FIELDS);
            printf("\n-- %s --\n", POSITION_NAMES[i]);
            if (nf >= 2 && strcmp(fields[0], "OK") == 0) {
                int count = atoi(fields[1]);
                if (count == 0) printf("  (No candidates)\n");
                for (int j = 0; j < count; j++) printf("  %d. %-20s : %s votes\n", j+1, fields[2+j*2], fields[3+j*2]);
            }
        }
    }
}

/* ── Entry Point ─────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    if (argc >= 2) { 
        strncpy(g_server_ip, argv[1], 127); 
        g_server_ip[127] = '\0';
    }
    if (argc >= 3) { 
        g_server_port = atoi(argv[2]); 
    }

    int choice;
    do {
        printf("\n==========================================\n");
        printf("    SONU VOTING SYSTEM - GLOBAL CLIENT\n");
        printf("    Target: %s:%d\n", g_server_ip, g_server_port);
        printf("==========================================\n");
        printf("  1. Voter Portal\n  2. Candidate Portal\n  3. View Results\n  4. Exit\n");
        choice = read_int("  Choice: ");
        switch (choice) {
            case 1: voter_portal(); break;
            case 2: candidate_portal(); break;
            case 3: results_menu(); break;
        }
    } while (choice != 4);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}