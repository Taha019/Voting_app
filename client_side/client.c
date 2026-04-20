/* ═══════════════════════════════════════════════════════════════════
 * SONU Electronic Voting System — Assignment 3
 * UDP Client  (Optimized for playit.gg / Remote Access)
 * ═══════════════════════════════════════════════════════════════════ */

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef int socklen_t;
  #define CLOSE_SOCK(s)  closesocket(s)
  #define BAD_SOCK(s)    ((s) == INVALID_SOCKET)
  typedef SOCKET sock_t;
#else
  #define _POSIX_C_SOURCE 200809L
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <sys/time.h>    
  #define CLOSE_SOCK(s)  close(s)
  #define BAD_SOCK(s)    ((s) < 0)
  typedef int sock_t;
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Inline POSITIONS_H Logic ────────────────────────────────────── */
#define MAX_POSITIONS     5
#define MAX_NAME_LEN      64
#define POS_CHAIRMAN      0
#define POS_VICE_CHAIRMAN 1
#define POS_SECRETARY     2
#define POS_TREASURER     3
#define POS_PRO           4

static const char * const POSITION_NAMES[MAX_POSITIONS] = {
    "Chairman", "Vice Chairman", "Secretary", "Treasurer", "PRO"
};

/* ── Global Configuration ────────────────────────────────────────── */
#define BUFFER_SIZE 4096
static char g_server_ip[128] = "147.185.221.223"; // Your Playit IP
static int  g_server_port = 3467;                 // Your Playit Port

/* ── UI Helpers ──────────────────────────────────────────────────── */
static void print_line(void) { printf("------------------------------------------\n"); }

static void read_line(const char *prompt, char *buf, int size) {
    printf("%s", prompt);
    fgets(buf, size, stdin);
    buf[strcspn(buf, "\r\n")] = '\0';
}

static int read_int(const char *prompt) {
    char buf[32];
    read_line(prompt, buf, sizeof(buf));
    return atoi(buf);
}

/* ── Network Engine ──────────────────────────────────────────────── */
static int send_command(const char *command, char *response, int resp_size) {
    sock_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (BAD_SOCK(sock)) return 0;

    struct hostent *he = gethostbyname(g_server_ip);
    if (he == NULL) {
        CLOSE_SOCK(sock);
        return 0;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_server_port);
    memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);

    sendto(sock, command, strlen(command), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

    // 3-second receive timeout
#ifdef _WIN32
    DWORD timeout = 3000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv; tv.tv_sec = 3; tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    int bytes = recvfrom(sock, response, resp_size - 1, 0, (struct sockaddr *)&from_addr, &from_len);
    
    CLOSE_SOCK(sock);
    if (bytes > 0) {
        response[bytes] = '\0';
        return 1;
    }
    return 0;
}

/* ── Portal Logic ────────────────────────────────────────────────── */

void view_results() {
    print_line();
    printf("         LIVE ELECTION RESULTS\n");
    print_line();
    for (int i = 0; i < MAX_POSITIONS; i++) {
        char cmd[64], resp[BUFFER_SIZE];
        sprintf(cmd, "GET_RESULTS|%d", i);
        if (send_command(cmd, resp, BUFFER_SIZE)) {
            printf("\n[%s]\n", POSITION_NAMES[i]);
            // Simple parsing of "OK|count|name|votes..."
            char *token = strtok(resp, "|"); // OK
            token = strtok(NULL, "|");       // count
            int count = token ? atoi(token) : 0;
            if (count == 0) printf("  (No candidates)\n");
            for (int j = 0; j < count; j++) {
                char *name = strtok(NULL, "|");
                char *votes = strtok(NULL, "|");
                printf("  %-20s : %s votes\n", name, votes);
            }
        }
    }
}

void voter_portal() {
    char user[64], pass[64], cmd[256], resp[BUFFER_SIZE];
    printf("\n--- VOTER LOGIN ---\n");
    read_line("Username: ", user, 64);
    read_line("Password: ", pass, 64);

    sprintf(cmd, "LOGIN_VOTER|%s|%s", user, pass);
    if (!send_command(cmd, resp, BUFFER_SIZE) || strncmp(resp, "OK", 2) != 0) {
        printf("[!] Login failed.\n");
        return;
    }

    char *token = strtok(resp, "|"); // OK
    char *v_id = strtok(NULL, "|");
    char *v_name = strtok(NULL, "|");
    printf("\nWelcome, %s (ID: %s)\n", v_name, v_id);

    // Voting loop
    for (int i = 0; i < MAX_POSITIONS; i++) {
        printf("\nPosition: %s\n", POSITION_NAMES[i]);
        sprintf(cmd, "LIST_CANDS|%d", i);
        send_command(cmd, resp, BUFFER_SIZE);
        
        printf("Candidates:\n%s\n", resp);
        int c_id = read_int("Enter Candidate ID to vote (0 to skip): ");
        if (c_id > 0) {
            sprintf(cmd, "CAST_VOTE|%s|%d|%d", v_id, c_id, i);
            send_command(cmd, resp, BUFFER_SIZE);
            printf("Server: %s\n", resp);
        }
    }
}

/* ── Main Entry ──────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    if (argc >= 3) {
        strncpy(g_server_ip, argv[1], sizeof(g_server_ip) - 1);
        g_server_port = atoi(argv[2]);
    }

    int choice;
    do {
        printf("\n==========================================\n");
        printf("    SONU VOTING SYSTEM - CLIENT\n");
        printf("    Connected to: %s:%d\n", g_server_ip, g_server_port);
        printf("==========================================\n");
        printf("1. Voter Portal\n2. View Results\n3. Exit\nChoice: ");
        choice = read_int("");

        switch (choice) {
            case 1: voter_portal(); break;
            case 2: view_results(); break;
        }
    } while (choice != 3);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}