/* ═══════════════════════════════════════════════════════════════════
 *  SONU Electronic Voting System — Assignment 3
 *  UDP Client (POSIX / Linux)
 *
 *  Sends each command as a single UDP datagram to the server and
 *  waits for the reply datagram.  No persistent connection is kept
 *  between operations — fully connectionless.
 *
 *  Build:
 *    gcc -Wall -std=c99 -o bin/client src/client.c
 * ═══════════════════════════════════════════════════════════════════ */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "../headers/positions.h"

#define SERVER_IP    "127.0.0.1"
#define SERVER_PORT  8080
#define BUFFER_SIZE  4096
#define MAX_FIELDS   128
#define RECV_TIMEOUT 5      /* seconds to wait for a server reply */

/* ── Network helper (UDP) ────────────────────────────────────────── *
 * Creates a UDP socket, sends one datagram, waits for the reply,
 * then closes the socket.  One socket per request — fully
 * connectionless, matching the UDP server design.
 * Returns 1 on success, 0 on failure.
 */
static int send_command(const char *command, char *response, int resp_size)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        printf("  [!] socket() failed.\n");
        return 0;
    }

    /* Set a receive timeout so we do not block forever */
    struct timeval tv;
    tv.tv_sec  = RECV_TIMEOUT;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port        = htons(SERVER_PORT);

    /* Send the command datagram */
    ssize_t sent = sendto(sock, command, strlen(command), 0,
                          (struct sockaddr *)&server_addr,
                          sizeof(server_addr));
    if (sent < 0) {
        printf("  [!] sendto() failed — is the server running?\n");
        close(sock);
        return 0;
    }

    /* Wait for the reply datagram */
    memset(response, 0, resp_size);
    ssize_t bytes = recvfrom(sock, response, resp_size - 1, 0, NULL, NULL);
    close(sock);

    if (bytes < 0) {
        printf("  [!] No response from server (timeout after %ds).\n",
               RECV_TIMEOUT);
        return 0;
    }

    response[bytes] = '\0';
    response[strcspn(response, "\r\n")] = '\0';   /* Strip newline */
    return 1;
}

/* ── Input helpers ───────────────────────────────────────────────── */

static void read_line(const char *prompt, char *buf, int size)
{
    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buf, size, stdin) == NULL) { buf[0] = '\0'; return; }
    buf[strcspn(buf, "\r\n")] = '\0';
}

static int read_int(const char *prompt)
{
    char buf[32];
    read_line(prompt, buf, sizeof(buf));
    return atoi(buf);
}

static void print_line(void)
{
    printf("  -----------------------------------------\n");
}

/* ── Pipe-delimited response parser ──────────────────────────────── */
static int parse_response(char *resp, char *fields[], int max_fields)
{
    int count = 0;
    char *token = strtok(resp, "|");
    while (token && count < max_fields) {
        fields[count++] = token;
        token = strtok(NULL, "|");
    }
    return count;
}

/* ── Position picker ─────────────────────────────────────────────── */
static int choose_position(void)
{
    printf("\n  Positions:\n");
    for (int i = 0; i < MAX_POSITIONS; i++)
        printf("    %d. %s\n", i, POSITION_NAMES[i]);
    int pos = read_int("  Enter Position ID (0-4): ");
    if (pos < 0 || pos >= MAX_POSITIONS) {
        printf("  [!] Invalid position ID.\n");
        return -1;
    }
    return pos;
}

/* ── Voting Booth ────────────────────────────────────────────────── */
static void voting_booth(int voter_id, const char *voter_name)
{
    printf("\n  Welcome, %s!  (Voter ID: %d)\n", voter_name, voter_id);

    int   choice;
    char  cmd[BUFFER_SIZE], resp[BUFFER_SIZE], resp_copy[BUFFER_SIZE];
    char *fields[MAX_FIELDS];

    do {
        printf("\n=== VOTING BOOTH ===\n");
        printf("  1. List Candidates for a Position\n");
        printf("  2. Cast Vote\n");
        printf("  3. My Voting Status\n");
        printf("  4. Logout\n");
        print_line();
        choice = read_int("  Choice: ");

        switch (choice) {

        /* ── List candidates ─────────────────────────────────────── */
        case 1: {
            int pos = choose_position();
            if (pos < 0) break;

            snprintf(cmd, sizeof(cmd), "LIST_CANDS|%d\n", pos);
            if (!send_command(cmd, resp, sizeof(resp))) break;

            strncpy(resp_copy, resp, sizeof(resp_copy) - 1);
            resp_copy[sizeof(resp_copy) - 1] = '\0';
            int nf = parse_response(resp_copy, fields, MAX_FIELDS);

            if (nf >= 1 && strcmp(fields[0], "OK") == 0) {
                int count = (nf >= 2) ? atoi(fields[1]) : 0;
                printf("\n  Candidates for %s:\n", POSITION_NAMES[pos]);
                if (count == 0) {
                    printf("  (none registered)\n");
                } else {
                    printf("  %-5s  %-28s  %s\n", "ID", "Name", "Votes");
                    printf("  %-5s  %-28s  %s\n",
                           "-----", "----------------------------", "-----");
                    for (int i = 0; i < count; i++) {
                        int base = 2 + i * 3;
                        if (base + 2 < nf)
                            printf("  %-5s  %-28s  %s\n",
                                   fields[base], fields[base+1], fields[base+2]);
                    }
                }
            } else {
                printf("  [ERR] %s\n", (nf >= 2) ? fields[1] : "Unknown error");
            }
            break;
        }

        /* ── Cast vote ───────────────────────────────────────────── */
        case 2: {
            int pos = choose_position();
            if (pos < 0) break;

            snprintf(cmd, sizeof(cmd), "LIST_CANDS|%d\n", pos);
            if (!send_command(cmd, resp, sizeof(resp))) break;

            strncpy(resp_copy, resp, sizeof(resp_copy) - 1);
            resp_copy[sizeof(resp_copy) - 1] = '\0';
            int nf = parse_response(resp_copy, fields, MAX_FIELDS);

            if (nf < 2 || strcmp(fields[0], "OK") != 0) {
                printf("  [ERR] Could not fetch candidates.\n");
                break;
            }
            int count = atoi(fields[1]);
            if (count == 0) {
                printf("  [!] No candidates registered for %s.\n",
                       POSITION_NAMES[pos]);
                break;
            }

            printf("  %-5s  %-28s\n", "ID", "Name");
            printf("  %-5s  %-28s\n", "-----", "----------------------------");
            for (int i = 0; i < count; i++) {
                int base = 2 + i * 3;
                if (base + 1 < nf)
                    printf("  %-5s  %-28s\n", fields[base], fields[base+1]);
            }

            int cid = read_int("  Enter Candidate ID: ");
            snprintf(cmd, sizeof(cmd), "CAST_VOTE|%d|%d|%d\n",
                     voter_id, cid, pos);
            if (!send_command(cmd, resp, sizeof(resp))) break;

            strncpy(resp_copy, resp, sizeof(resp_copy) - 1);
            resp_copy[sizeof(resp_copy) - 1] = '\0';
            nf = parse_response(resp_copy, fields, MAX_FIELDS);

            if (nf >= 1 && strcmp(fields[0], "OK") == 0)
                printf("  [OK]  Vote cast successfully for %s!\n",
                       POSITION_NAMES[pos]);
            else if (nf >= 2) {
                if (strcmp(fields[1], "ALREADY_VOTED") == 0)
                    printf("  [ERR] You have already voted for %s.\n",
                           POSITION_NAMES[pos]);
                else if (strcmp(fields[1], "INVALID_CANDIDATE") == 0)
                    printf("  [ERR] Invalid candidate ID or position mismatch.\n");
                else if (strcmp(fields[1], "VOTER_NOT_FOUND") == 0)
                    printf("  [ERR] Voter record not found.\n");
                else
                    printf("  [ERR] %s\n", fields[1]);
            }
            break;
        }

        /* ── Voting status ───────────────────────────────────────── */
        case 3: {
            snprintf(cmd, sizeof(cmd), "VOTER_STATUS|%d\n", voter_id);
            if (!send_command(cmd, resp, sizeof(resp))) break;

            strncpy(resp_copy, resp, sizeof(resp_copy) - 1);
            resp_copy[sizeof(resp_copy) - 1] = '\0';
            int nf = parse_response(resp_copy, fields, MAX_FIELDS);

            if (nf >= 2 && strcmp(fields[0], "OK") == 0) {
                printf("\n  Voting Status  —  %s\n", fields[1]);
                printf("  %-22s  %s\n", "Position", "Status");
                printf("  %-22s  %s\n",
                       "----------------------", "----------");
                for (int i = 0; i < MAX_POSITIONS; i++) {
                    int flag = (2 + i < nf) ? atoi(fields[2 + i]) : 0;
                    printf("  %-22s  %s\n",
                           POSITION_NAMES[i],
                           flag ? "[VOTED]" : "[PENDING]");
                }
            } else {
                printf("  [ERR] Could not retrieve voting status.\n");
            }
            break;
        }

        case 4:
            printf("  Logged out.  Goodbye, %s!\n", voter_name);
            break;

        default:
            printf("  [!] Invalid choice.\n");
        }
    } while (choice != 4);
}

/* ── Voter Portal ────────────────────────────────────────────────── */
static void voter_portal(void)
{
    int   choice;
    char  cmd[BUFFER_SIZE], resp[BUFFER_SIZE], resp_copy[BUFFER_SIZE];
    char *fields[16];

    do {
        printf("\n=== VOTER PORTAL ===\n");
        printf("  1. Register\n");
        printf("  2. Login\n");
        printf("  3. Back\n");
        print_line();
        choice = read_int("  Choice: ");

        if (choice == 1) {
            char name[MAX_NAME_LEN], user[MAX_NAME_LEN], pass[MAX_PASS_LEN];
            printf("\n  -- Voter Registration --\n");
            read_line("  Full Name : ", name, sizeof(name));
            read_line("  Username  : ", user, sizeof(user));
            read_line("  Password  : ", pass, sizeof(pass));

            snprintf(cmd, sizeof(cmd), "REG_VOTER|%s|%s|%s\n",
                     name, user, pass);
            if (send_command(cmd, resp, sizeof(resp))) {
                strncpy(resp_copy, resp, sizeof(resp_copy) - 1);
                resp_copy[sizeof(resp_copy) - 1] = '\0';
                int nf = parse_response(resp_copy, fields, 16);
                if (nf >= 2 && strcmp(fields[0], "OK") == 0)
                    printf("  [OK]  Registered!  Your Voter ID: %s\n",
                           fields[1]);
                else
                    printf("  [ERR] Username already taken.  Choose another.\n");
            }

        } else if (choice == 2) {
            char user[MAX_NAME_LEN], pass[MAX_PASS_LEN];
            printf("\n  -- Voter Login --\n");
            read_line("  Username : ", user, sizeof(user));
            read_line("  Password : ", pass, sizeof(pass));

            snprintf(cmd, sizeof(cmd), "LOGIN_VOTER|%s|%s\n", user, pass);
            if (send_command(cmd, resp, sizeof(resp))) {
                strncpy(resp_copy, resp, sizeof(resp_copy) - 1);
                resp_copy[sizeof(resp_copy) - 1] = '\0';
                int nf = parse_response(resp_copy, fields, 16);
                if (nf >= 3 && strcmp(fields[0], "OK") == 0) {
                    int id = atoi(fields[1]);
                    voting_booth(id, fields[2]);
                } else {
                    printf("  [ERR] Invalid credentials.\n");
                }
            }
        }
    } while (choice != 3);
}

/* ── Candidate Portal ────────────────────────────────────────────── */
static void candidate_portal(void)
{
    int   choice;
    char  cmd[BUFFER_SIZE], resp[BUFFER_SIZE], resp_copy[BUFFER_SIZE];
    char *fields[16];

    do {
        printf("\n=== CANDIDATE PORTAL ===\n");
        printf("  1. Register\n");
        printf("  2. Login\n");
        printf("  3. Back\n");
        print_line();
        choice = read_int("  Choice: ");

        if (choice == 1) {
            char name[MAX_NAME_LEN], user[MAX_NAME_LEN], pass[MAX_PASS_LEN];
            printf("\n  -- Candidate Registration --\n");
            read_line("  Full Name  : ", name, sizeof(name));
            read_line("  Username   : ", user, sizeof(user));
            read_line("  Password   : ", pass, sizeof(pass));

            printf("\n  Positions:\n");
            for (int i = 0; i < MAX_POSITIONS; i++)
                printf("    %d. %s\n", i, POSITION_NAMES[i]);
            int pos = read_int("  Position ID (0-4): ");

            snprintf(cmd, sizeof(cmd), "REG_CAND|%s|%s|%s|%d\n",
                     name, user, pass, pos);
            if (send_command(cmd, resp, sizeof(resp))) {
                strncpy(resp_copy, resp, sizeof(resp_copy) - 1);
                resp_copy[sizeof(resp_copy) - 1] = '\0';
                int nf = parse_response(resp_copy, fields, 16);
                if (nf >= 2 && strcmp(fields[0], "OK") == 0)
                    printf("  [OK]  Registered!  Your Candidate ID: %s\n",
                           fields[1]);
                else if (nf >= 2 && strcmp(fields[1], "INVALID_POSITION") == 0)
                    printf("  [ERR] Invalid position ID.\n");
                else
                    printf("  [ERR] Username already taken.  Choose another.\n");
            }

        } else if (choice == 2) {
            char user[MAX_NAME_LEN], pass[MAX_PASS_LEN];
            printf("\n  -- Candidate Login --\n");
            read_line("  Username : ", user, sizeof(user));
            read_line("  Password : ", pass, sizeof(pass));

            snprintf(cmd, sizeof(cmd), "LOGIN_CAND|%s|%s\n", user, pass);
            if (send_command(cmd, resp, sizeof(resp))) {
                strncpy(resp_copy, resp, sizeof(resp_copy) - 1);
                resp_copy[sizeof(resp_copy) - 1] = '\0';
                int nf = parse_response(resp_copy, fields, 16);
                if (nf >= 5 && strcmp(fields[0], "OK") == 0) {
                    printf("\n  === CANDIDATE DASHBOARD ===\n");
                    printf("  Name     : %s\n", fields[2]);
                    printf("  Position : %s\n", fields[3]);
                    printf("  Votes    : %s\n", fields[4]);
                    printf("  ID       : %s\n", fields[1]);
                    printf("\n  (Press Enter to continue)\n");
                    char tmp[4];
                    read_line("", tmp, sizeof(tmp));
                } else {
                    printf("  [ERR] Invalid credentials.\n");
                }
            }
        }
    } while (choice != 3);
}

/* ── Results Viewer ──────────────────────────────────────────────── */
static void results_menu(void)
{
    int   choice;
    char  cmd[BUFFER_SIZE], resp[BUFFER_SIZE], resp_copy[BUFFER_SIZE];
    char *fields[MAX_FIELDS];

    do {
        printf("\n=== RESULTS VIEWER ===\n");
        printf("  1. Results by Position\n");
        printf("  2. All Results\n");
        printf("  3. Back\n");
        print_line();
        choice = read_int("  Choice: ");

        if (choice == 1) {
            int pos = choose_position();
            if (pos < 0) continue;

            snprintf(cmd, sizeof(cmd), "GET_RESULTS|%d\n", pos);
            if (!send_command(cmd, resp, sizeof(resp))) continue;

            strncpy(resp_copy, resp, sizeof(resp_copy) - 1);
            resp_copy[sizeof(resp_copy) - 1] = '\0';
            int nf = parse_response(resp_copy, fields, MAX_FIELDS);

            if (nf >= 2 && strcmp(fields[0], "OK") == 0) {
                int count = atoi(fields[1]);
                printf("\n  Results  —  %s\n", POSITION_NAMES[pos]);
                printf("  %-4s  %-28s  %s\n", "Rank", "Name", "Votes");
                printf("  %-4s  %-28s  %s\n",
                       "----", "----------------------------", "-----");
                if (count == 0) {
                    printf("  (no candidates registered)\n");
                } else {
                    for (int i = 0; i < count; i++) {
                        int base = 2 + i * 2;
                        if (base + 1 < nf)
                            printf("  %-4d  %-28s  %s\n",
                                   i + 1, fields[base], fields[base+1]);
                    }
                }
            } else {
                printf("  [ERR] %s\n", (nf >= 2) ? fields[1] : "Unknown error");
            }

        } else if (choice == 2) {
            printf("\n=== FULL ELECTION RESULTS ===\n");
            for (int pos = 0; pos < MAX_POSITIONS; pos++) {
                snprintf(cmd, sizeof(cmd), "GET_RESULTS|%d\n", pos);
                if (!send_command(cmd, resp, sizeof(resp))) continue;

                strncpy(resp_copy, resp, sizeof(resp_copy) - 1);
                resp_copy[sizeof(resp_copy) - 1] = '\0';
                int nf = parse_response(resp_copy, fields, MAX_FIELDS);

                printf("\n  %-28s\n", POSITION_NAMES[pos]);
                printf("  %.28s\n", "----------------------------");

                if (nf >= 2 && strcmp(fields[0], "OK") == 0) {
                    int count = atoi(fields[1]);
                    if (count == 0) {
                        printf("  (no candidates registered)\n");
                    } else {
                        printf("  %-4s  %-28s  %s\n",
                               "Rank", "Name", "Votes");
                        printf("  %-4s  %-28s  %s\n",
                               "----", "----------------------------", "-----");
                        for (int i = 0; i < count; i++) {
                            int base = 2 + i * 2;
                            if (base + 1 < nf)
                                printf("  %-4d  %-28s  %s\n",
                                       i + 1, fields[base], fields[base+1]);
                        }
                    }
                }
            }
        }
    } while (choice != 3);
}

/* ── Entry point ─────────────────────────────────────────────────── */
int main(void)
{
    printf("==========================================\n");
    printf("    ELECTRONIC VOTING SYSTEM  v3.0       \n");
    printf("    Assignment 3  —  UDP Connectionless  \n");
    printf("==========================================\n");

    int choice;
    do {
        printf("\n=== MAIN MENU ===\n");
        printf("  1. Voter Portal\n");
        printf("  2. Candidate Portal\n");
        printf("  3. View Results\n");
        printf("  4. Exit\n");
        print_line();
        choice = read_int("  Choice: ");

        switch (choice) {
        case 1: voter_portal();     break;
        case 2: candidate_portal(); break;
        case 3: results_menu();     break;
        case 4: printf("\n  Goodbye!\n\n"); break;
        default: printf("  [!] Invalid choice.\n");
        }
    } while (choice != 4);

    return 0;
}
