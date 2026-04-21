# SONU Electronic Voting System

> **UDP Connectionless Server | Linux/POSIX | fork() per Request | Global Access via Tailscale**

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [Project Structure](#project-structure)
3. [Architecture](#architecture)
4. [Application Protocol](#application-protocol)
5. [Building the Project](#building-the-project)
6. [Running the System](#running-the-system)
7. [Network Connectivity Guide](#network-connectivity-guide)
8. [How the Code Works](#how-the-code-works)
9. [Data Files](#data-files)
10. [Firewall & Port Reference](#firewall--port-reference)
11. [Troubleshooting](#troubleshooting)

---

## Project Overview

This is the **SONU Electronic Voting System** — a fully functional electronic voting application built from scratch in C. It allows voters to register, log in, cast votes for candidates across multiple positions, and view results in real time.

The system uses a **Linux/POSIX, connectionless UDP server** that `fork()`s a child process for every incoming client request. It supports global connectivity via **Tailscale**, making it accessible from any network without complex router configuration.

---

## Project Structure

```
assignment3/
├── server_side/                  ← Linux UDP server + all business logic
│   ├── src/
│   │   ├── server.c              ← Main server: UDP socket, fork() per request
│   │   ├── voter.c               ← Voter registration, login, status
│   │   ├── candidate.c           ← Candidate registration, login, lookup
│   │   ├── election.c            ← Vote casting + result tallying
│   │   └── file_io.c             ← Binary flat-file helpers
│   ├── headers/
│   │   ├── positions.h           ← Compile-time position constants (shared)
│   │   ├── voter.h               ← Voter struct definition
│   │   ├── candidate.h           ← Candidate struct definition
│   │   ├── election.h            ← Vote result codes
│   │   └── file_io.h             ← File helper declarations
│   ├── data/
│   │   ├── voters.dat            ← Binary flat file of Voter structs
│   │   └── candidates.dat        ← Binary flat file of Candidate structs
│   ├── obj/                      ← Compiled object files (auto-generated)
│   ├── bin/
│   │   └── server                ← Compiled server binary (auto-generated)
│   └── Makefile
└── client_side/
    ├── client.c                  ← Cross-platform UDP client source
    └── client                    ← Compiled client binary (auto-generated)
```

---

## Architecture

### Component Roles

| Component | Description |
|---|---|
| `server.c` | UDP server bound to `0.0.0.0:8080`. Listens on all network interfaces. For each incoming datagram it `fork()`s a child process that handles the command and sends the reply, while the parent immediately loops back to `recvfrom()`. |
| `client.c` | Menu-driven terminal UI. For each operation it opens a fresh UDP socket, sends one datagram to the server, waits up to 5 seconds for the reply, then closes the socket. Accepts the server IP and port as optional command-line arguments. |
| `voter.c` | Voter registration, login, record lookup, and voted-position marking. All file I/O on `voters.dat`. |
| `candidate.c` | Candidate registration, login, lookup by ID, and vote-count increment. All file I/O on `candidates.dat`. |
| `election.c` | `cast_vote()` (two-stage security check + atomic write) and `generate_results()` (scan + descending sort). |
| `file_io.c` | Centralises file paths. Provides `open_voters_file()`, `open_candidates_file()`, and `get_record_count()`. |

---

## Application Protocol

All messages are **pipe-delimited plain-text strings terminated by `\n`**. Each client operation is exactly **one datagram sent and one datagram received** — no session state is maintained between operations.

| Operation | Client → Server | Server → Client |
|---|---|---|
| Register voter | `REG_VOTER\|name\|user\|pass` | `OK\|voter_id` or `ERR\|USERNAME_TAKEN` |
| Register candidate | `REG_CAND\|name\|user\|pass\|pos_id` | `OK\|cand_id` or `ERR\|INVALID_POSITION` / `ERR\|USERNAME_TAKEN` |
| Login voter | `LOGIN_VOTER\|user\|pass` | `OK\|voter_id\|full_name` or `ERR\|INVALID_CREDENTIALS` |
| Login candidate | `LOGIN_CAND\|user\|pass` | `OK\|cand_id\|name\|position\|votes` or `ERR\|INVALID_CREDENTIALS` |
| Cast vote | `CAST_VOTE\|voter_id\|cand_id\|pos_id` | `OK\|VOTE_CAST` or `ERR\|ALREADY_VOTED` / `ERR\|INVALID_CANDIDATE` |
| Get results | `GET_RESULTS\|pos_id` | `OK\|count\|name1\|votes1\|name2\|votes2\|...` or `ERR\|INVALID_POSITION` |
| List candidates | `LIST_CANDS\|pos_id` | `OK\|count\|id1\|name1\|votes1\|...` or `ERR\|INVALID_POSITION` |
| Voter status | `VOTER_STATUS\|voter_id` | `OK\|name\|flag0\|flag1\|flag2\|flag3\|flag4` (flag = 1 if voted) |

### Position IDs

| ID | Position |
|---|---|
| 0 | Chairman |
| 1 | Vice Chairman |
| 2 | Secretary |
| 3 | Treasurer |
| 4 | PRO |

---

## Building the Project

Requires **GCC** and standard POSIX libraries. No extra packages needed on any modern Linux distribution.

### Build the Server (Linux)

```bash
cd server_side
make
```

This produces `bin/server`. To rebuild from scratch:

```bash
make clean && make
```

### Build the Client

**On Linux:**
```bash
cd client_side
gcc -o client client.c
```

**On Windows (MinGW / MSYS2):**
```bash
cd client_side
gcc -o client client.c -lws2_32
```

---

## Running the System

### Same Machine (Local Testing)

```bash
# Terminal 1 — start the server
./server_side/bin/server

# Terminal 2 — connect client (defaults to 127.0.0.1)
./client_side/client 127.0.0.1 8080
```

### Same Local Network (LAN)

The server prints its local IP addresses at startup:

```
[SERVER] Local interfaces:
         lo            127.0.0.1
         eth0          192.168.1.42
```

On any other machine on the same network:

```bash
./client_side/client 192.168.1.42 8080
```

### Different Networks — Using Tailscale

Tailscale gives each machine a stable private IP (`100.x.x.x`) that works across any network — no port forwarding or router access needed. See [Network Connectivity Guide](#network-connectivity-guide) below.

---

## Network Connectivity Guide

### Option 1: Tailscale (Recommended)

**Step 1 — Install Tailscale on both machines**

On the Linux server:
```bash
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up
```

On the Windows client: download and install from [tailscale.com/download](https://tailscale.com/download), then sign in.

> Both machines must log into the **same Tailscale account**.

**Step 2 — Get the server's Tailscale IP**

```bash
tailscale ip -4
# Example output: 100.64.1.23
```

**Step 3 — Open the firewall on the server**

```bash
sudo ufw allow 8080/udp
sudo ufw reload
```

**Step 4 — Start the server**

```bash
./server_side/bin/server
```

**Step 5 — Connect the client using the Tailscale IP**

```bash
./client_side/client 100.64.1.23 8080
```

**Verify the tunnel before connecting:**
```bash
tailscale ping 100.64.1.23
```

---

### Option 2: Direct Internet (Port Forwarding)

1. **Open the firewall** on the server:
   ```bash
   sudo ufw allow 8080/udp
   ```

2. **Set up port forwarding** on the server's router:
   - Protocol: UDP
   - External port: 8080
   - Internal IP: server's LAN IP
   - Internal port: 8080

3. **Find the server's public IP:**
   ```bash
   curl ifconfig.me
   ```

4. **Connect the client:**
   ```bash
   ./client_side/client 203.0.113.5 8080
   ```

---

### Connectivity Summary

| Scenario | What to do |
|---|---|
| Same machine | Nothing — `127.0.0.1` works out of the box |
| Same LAN | `sudo ufw allow 8080/udp`, use server's LAN IP |
| Different networks | **Tailscale** (easiest) or port forwarding |
| No router access | **Tailscale** |

---

## How the Code Works

---

### server.c — Detailed Walkthrough

#### 1. Signal Handling — Preventing Zombie Processes

```c
signal(SIGCHLD, SIG_IGN);
```

When a child process finishes, it sends a `SIGCHLD` signal to its parent. Normally the parent must call `wait()` to clean up the child — if it doesn't, the child becomes a **zombie process** (still occupies an entry in the process table even though it's dead). Because the server forks a new child for every single request, doing nothing would fill the process table with zombies and eventually crash the server.

Setting `SIGCHLD` to `SIG_IGN` tells the kernel: *"automatically reap children the moment they exit, no `wait()` needed."* This one line prevents zombie accumulation for the entire lifetime of the server.

---

#### 2. Creating the UDP Socket

```c
int server_sock = socket(AF_INET, SOCK_DGRAM, 0);
```

- `AF_INET` — use IPv4 addressing.
- `SOCK_DGRAM` — this is a UDP socket. Unlike `SOCK_STREAM` (TCP), UDP is **connectionless** — there is no handshake, no established connection, and no guarantee of delivery or ordering. Each message is an independent datagram.
- The return value `server_sock` is a **file descriptor** — an integer handle the OS gives us to refer to this socket in all future calls.

```c
int opt = 1;
setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

`SO_REUSEADDR` allows the server to bind to port 8080 immediately after a restart, even if the port is still in a `TIME_WAIT` state from the previous run. Without this, you would get `bind() failed: Address already in use` and have to wait before restarting.

---

#### 3. Binding to All Interfaces

```c
server_addr.sin_family      = AF_INET;
server_addr.sin_addr.s_addr = INADDR_ANY;
server_addr.sin_port        = htons(SERVER_PORT);
bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
```

- `INADDR_ANY` means the server listens on **all** network interfaces simultaneously — localhost (`127.0.0.1`), the LAN IP (e.g. `192.168.x.x`), and any Tailscale interface (`100.x.x.x`). The server doesn't need to know its own IP.
- `htons(SERVER_PORT)` converts the port number from **host byte order** to **network byte order** (big-endian). All port and IP values sent over the network must be in network byte order — forgetting this is a common networking bug.
- `bind()` reserves port 8080 so the OS knows to deliver incoming UDP datagrams to this socket.

---

#### 4. Printing Local IP Addresses (`print_local_ips`)

```c
struct ifaddrs *ifap, *ifa;
getifaddrs(&ifap);
for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr->sa_family != AF_INET) continue;
    struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
    printf("%-12s  %s\n", ifa->ifa_name, inet_ntoa(sa->sin_addr));
}
freeifaddrs(ifap);
```

`getifaddrs()` queries the OS for every network interface on the machine and returns a linked list. We walk the list, filter for IPv4 addresses only (`AF_INET`), and print the interface name and IP. This tells the user exactly which IPs they can give to remote clients. `freeifaddrs()` releases the allocated memory when done.

---

#### 5. The Main Receive Loop

```c
while (1) {
    ssize_t bytes_recv = recvfrom(server_sock, buffer, BUFFER_SIZE - 1, 0,
                                  (struct sockaddr *)&client_addr, &addr_len);
```

`recvfrom()` **blocks** — the process sleeps here doing nothing until a UDP datagram arrives. When one does arrive, it:
- Copies the datagram payload into `buffer`.
- Fills `client_addr` with the sender's IP address and port number. This is critical — because UDP is connectionless, the server must capture *who* sent the datagram in order to know where to send the reply.

---

#### 6. Forking a Child to Handle the Request

```c
pid_t pid = fork();

if (pid == 0) {
    // CHILD
    handle_command(buffer, response, BUFFER_SIZE);
    sendto(server_sock, response, strlen(response), 0,
           (struct sockaddr *)&client_addr, addr_len);
    close(server_sock);
    exit(0);
}
// PARENT falls through and loops back to recvfrom()
```

`fork()` creates an exact copy of the running process. Both parent and child continue executing from the same point — the only difference is the return value:
- In the **child**, `fork()` returns `0`.
- In the **parent**, `fork()` returns the child's PID (a positive integer).

The **child** process handles the command, sends the reply back to `client_addr` (which it inherited from the parent), closes its copy of the socket, and exits cleanly.

The **parent** logs the child PID and immediately loops back to `recvfrom()` to wait for the next datagram. It is never blocked by anything the child does. This is what allows the server to handle multiple clients at the same time.

---

#### 7. `handle_command()` — The Command Dispatcher

```c
char cmd[BUFFER_SIZE];
strncpy(cmd, raw, BUFFER_SIZE - 1);
cmd[strcspn(cmd, "\r\n")] = '\0';
```

The raw buffer is copied into a local `cmd` array so it can be modified safely by `strtok()`. `strcspn()` finds the position of the first `\r` or `\n` and replaces it with a null terminator — stripping the trailing newline before parsing.

```c
char *f[MAX_FIELDS];
int nf = parse_fields(cmd, f, MAX_FIELDS);
```

`parse_fields()` calls `strtok()` repeatedly, splitting the string on `|` delimiters and storing a pointer to each token in the `f[]` array. After parsing `"REG_VOTER|Alice|alice99|pass123"`, the result is: `f[0]="REG_VOTER"`, `f[1]="Alice"`, `f[2]="alice99"`, `f[3]="pass123"`.

```c
if (strcmp(f[0], "REG_VOTER") == 0) { ... }
else if (strcmp(f[0], "LOGIN_VOTER") == 0) { ... }
// ... and so on
```

A chain of `strcmp()` checks on `f[0]` routes the request to the correct business-logic function. Each branch first validates that the expected number of fields (`nf`) is present, then calls the appropriate function from `voter.c`, `candidate.c`, or `election.c`, and writes the result into `resp` using `snprintf()`.

---

### client.c — Detailed Walkthrough

#### 1. Command-Line Arguments and Banner

```c
if (argc >= 2) strncpy(g_server_ip, argv[1], ...);
if (argc >= 3) g_server_port = atoi(argv[2]);
```

`argv[1]` is the server IP (defaults to `127.0.0.1`) and `argv[2]` is the port (defaults to `8080`). These are stored in globals so every function in the file can use them without needing to pass them as parameters everywhere.

The banner is printed once at startup, confirming the target so the user knows the client connected to the right server:
```
==========================================
    ELECTRONIC VOTING SYSTEM  v3.0
    Server  : 100.64.1.23:8080
==========================================
```

---

#### 2. `send_command()` — The Core of All Client Communication

Every menu action in the client — registering, logging in, voting — ultimately calls `send_command()`. This function performs one complete UDP round-trip and nothing else.

```c
int sock = socket(AF_INET, SOCK_DGRAM, 0);
```
A brand new socket is created for every single request. UDP sockets are lightweight, so this is fast. Creating a fresh socket each time also means there is no leftover state from a previous command that could interfere.

```c
struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
```
`SO_RCVTIMEO` sets a 5-second receive timeout on the socket. Without this, if the server never replies (because it is down or the network is broken), `recvfrom()` would block forever and the client would appear frozen. After 5 seconds with no reply, `recvfrom()` returns an error and the client can display a "timeout" message to the user instead.

```c
sendto(sock, command, strlen(command), 0,
       (struct sockaddr *)&server_addr, sizeof(server_addr));
```
The command string (e.g. `"LOGIN_VOTER|alice99|pass123\n"`) is sent as a single UDP datagram to the server's IP and port. Unlike TCP, there is no connection to maintain — this one call is all it takes to deliver the message.

```c
recvfrom(sock, response, BUFFER_SIZE - 1, 0, NULL, NULL);
close(sock);
```
The client waits for exactly one datagram back. The reply is stored in `response` and the socket is immediately closed. The `NULL` arguments for the sender address mean we don't need to verify who replied — in practice it will always be the server.

---

### voter.c — Detailed Walkthrough

#### `register_voter(name, username, password)`

```c
FILE *f = open_voters_file("rb");
Voter v;
while (fread(&v, sizeof(Voter), 1, f) == 1) {
    if (strcmp(v.username, username) == 0) {
        fclose(f); return -1;  // USERNAME_TAKEN
    }
}
fclose(f);
```

The file is opened in read-binary mode and scanned **record by record**. Each `fread()` reads exactly `sizeof(Voter)` bytes into a local `Voter` struct. If any existing record's username matches the incoming username, we return `-1` immediately without writing anything. This is a **linear scan** — it reads every record from the beginning of the file until either a match is found or the file ends.

```c
int id = get_record_count(VOTERS_FILE, sizeof(Voter)) + 1;
Voter new_voter;
memset(&new_voter, 0, sizeof(Voter));
strncpy(new_voter.full_name, name, ...);
new_voter.voter_id      = id;
new_voter.is_registered = 1;
// voted_positions[0..4] are all 0 from memset

FILE *fw = open_voters_file("ab");
fwrite(&new_voter, sizeof(Voter), 1, fw);
fclose(fw);
return id;
```

`memset(&new_voter, 0, sizeof(Voter))` zeroes the entire struct first. This guarantees `voted_positions[5]` starts as all zeros (meaning the voter hasn't voted anywhere yet) and prevents random garbage data from the stack from ending up in the file. The new ID is calculated as `current record count + 1`. The struct is then appended to the end of `voters.dat` in binary append mode (`"ab"`), which means the write always goes to the end of the file and cannot accidentally overwrite existing records.

---

#### `mark_position_voted(voter_id, position_id)`

```c
FILE *f = open_voters_file("r+b");
fseek(f, (voter_id - 1) * sizeof(Voter), SEEK_SET);
Voter v;
fread(&v, sizeof(Voter), 1, f);
v.voted_positions[position_id] = 1;
fseek(f, (voter_id - 1) * sizeof(Voter), SEEK_SET);
fwrite(&v, sizeof(Voter), 1, f);
fclose(f);
```

Because records are fixed-size and stored sequentially, record number `voter_id` is always at a predictable byte offset: `(voter_id - 1) * sizeof(Voter)`. `fseek()` jumps directly to that byte — this is **O(1) direct access**, like indexing into an array, regardless of how many voters are in the file. We read the struct, flip the flag for the relevant position to `1`, then seek back to the exact same offset and overwrite just that one record. No other records are touched.

---

### candidate.c — Detailed Walkthrough

#### `register_candidate(name, username, password, position_id)`

```c
if (position_id < 0 || position_id >= MAX_POSITIONS)
    return -2;  // INVALID_POSITION
```

The position ID is validated before touching any file. `MAX_POSITIONS` is defined in `positions.h` as a compile-time constant (5). Returning `-2` (a different code from the username-taken `-1`) lets the server send the correct, specific error message back to the client.

The duplicate username scan and binary append work identically to `register_voter`. The key additional field stored is `position_id`, which records which position (Chairman, Secretary, etc.) this candidate is running for. This field is checked later during vote casting to prevent a voter from casting a vote for a candidate in the wrong position's race.

---

#### `increment_vote_count(candidate_id)`

```c
FILE *f = open_candidates_file("r+b");
fseek(f, (candidate_id - 1) * sizeof(Candidate), SEEK_SET);
Candidate c;
fread(&c, sizeof(Candidate), 1, f);
c.vote_count++;
fseek(f, (candidate_id - 1) * sizeof(Candidate), SEEK_SET);
fwrite(&c, sizeof(Candidate), 1, f);
fclose(f);
```

Same direct-seek pattern as `mark_position_voted`. The candidate's record is fetched by its byte offset, the `vote_count` field is incremented by 1, and the modified struct is written back to the exact same position in the file. This is an in-place update — only 1 record is read and 1 record is written, no matter how large the database is.

---

### election.c — Detailed Walkthrough

#### `cast_vote(voter_id, candidate_id, position_id)`

This is the most security-critical function in the system. It enforces two completely independent checks before committing any write to disk.

**Check 1 — Voter eligibility:**

```c
Voter v;
if (!get_voter_by_id(voter_id, &v))       return VOTE_ERR_NOT_FOUND;
if (!v.is_registered)                     return VOTE_ERR_NOT_FOUND;
if (v.voted_positions[position_id] == 1)  return VOTE_ERR_ALREADY;
```

The voter record is loaded by ID. If it doesn't exist or `is_registered` is `0`, the vote is rejected immediately. Then `voted_positions[position_id]` is checked — if it's already `1`, the voter has previously voted in this position's race and is blocked from voting again. Each of the 5 positions is tracked independently, so a voter can legitimately vote for Chairman and then separately for Secretary, but cannot cast two votes for Chairman.

**Check 2 — Candidate validity:**

```c
Candidate c;
if (!get_candidate_by_id(candidate_id, &c)) return VOTE_ERR_INVALID;
if (!c.is_registered)                       return VOTE_ERR_INVALID;
if (c.position_id != position_id)           return VOTE_ERR_INVALID;
```

The candidate record is loaded by ID. The check `c.position_id != position_id` is a **cross-position fraud prevention** — it ensures the candidate actually belongs to the position the voter is trying to vote in. Without this, a malicious client could craft a packet like `CAST_VOTE|1|3|0` (trying to cast a vote for candidate #3, who runs for Secretary, into the Chairman race), which would be logically invalid and must be caught.

**Commit — only if both checks pass:**

```c
increment_vote_count(candidate_id);
mark_position_voted(voter_id, position_id);
return VOTE_OK;
```

Both writes happen here and only here, after all validation has passed. `increment_vote_count` updates the candidate's `vote_count` in `candidates.dat`; `mark_position_voted` sets the voter's `voted_positions[position_id]` flag to `1` in `voters.dat`. If either of the checks above had failed, neither file would have been touched.

---

#### `generate_results(position_id, results[], max_count)`

```c
FILE *f = open_candidates_file("rb");
Candidate c;
int count = 0;
while (fread(&c, sizeof(Candidate), 1, f) == 1) {
    if (c.is_registered && c.position_id == position_id)
        results[count++] = c;
}
fclose(f);
```

The entire candidates file is scanned linearly. Any candidate whose `position_id` matches the requested position and whose `is_registered == 1` is added to the `results` array.

```c
// Bubble sort descending by vote_count
for (int i = 0; i < count - 1; i++)
    for (int j = 0; j < count - i - 1; j++)
        if (results[j].vote_count < results[j+1].vote_count) {
            Candidate tmp = results[j];
            results[j]    = results[j+1];
            results[j+1]  = tmp;
        }
```

A bubble sort ranks candidates from highest to lowest `vote_count`. Since the number of candidates per position is small (typically under 10), bubble sort is perfectly adequate. The sorted array is then serialised into the pipe-delimited `OK|count|name1|votes1|name2|votes2|...` response and sent back to the client.

---

### file_io.c — Detailed Walkthrough

#### `get_record_count(filename, record_size)`

```c
FILE *f = fopen(filename, "rb");
if (!f) return 0;               // file doesn't exist yet → 0 records
fseek(f, 0, SEEK_END);         // jump to the end of the file
long size = ftell(f);          // get current byte position = file size
fclose(f);
return size / record_size;     // divide by struct size = record count
```

Because all records are fixed-size, the number of records is always `total_file_size / sizeof(struct)`. `fseek(f, 0, SEEK_END)` moves the file pointer to the very end, then `ftell()` returns the current byte position, which equals the total file size. Integer division by `record_size` gives the exact count. If the file doesn't exist yet (first run of the server), `fopen` returns `NULL` and the function returns `0` — the caller doesn't need any special handling for a missing file.

#### `open_voters_file(mode)` and `open_candidates_file(mode)`

```c
FILE *open_voters_file(const char *mode) {
    return fopen(VOTERS_FILE, mode);
}
```

These are thin wrappers around `fopen()`. Their purpose is to **centralise the file paths** — `VOTERS_FILE` and `CANDIDATES_FILE` are constants defined once in `file_io.h`. If the data directory or filenames ever need to change, only one line in one header needs updating rather than hunting down every `fopen()` call across the codebase.

---

## Data Files

Both files live in the `data/` directory and are **created automatically on first use**. They store fixed-size C structs sequentially — record `n` is always at byte offset `(n-1) * sizeof(struct)`, enabling O(1) direct-seek access.

| File | Struct | Key Fields |
|---|---|---|
| `data/voters.dat` | `Voter` | `voter_id`, `username`, `password`, `voted_positions[5]`, `is_registered` |
| `data/candidates.dat` | `Candidate` | `candidate_id`, `username`, `password`, `position_id`, `vote_count`, `is_registered` |

> The data files are accessed exclusively by the server. The client never reads them directly — all data access goes through the UDP protocol.

---

## Firewall & Port Reference

| Protocol | Port | Purpose |
|---|---|---|
| UDP | 8080 | Main server communication |

```bash
# Allow UDP 8080 through Linux firewall
sudo ufw allow 8080/udp
sudo ufw reload

# Check firewall status
sudo ufw status

# Find server's public IP (for internet access)
curl ifconfig.me
```

---

## Troubleshooting

| Problem | Cause | Fix |
|---|---|---|
| Client shows "Timeout" | Server unreachable | Check firewall (`sudo ufw status`), verify IP |
| `bind() failed` | Port 8080 already in use | `sudo lsof -i udp:8080` to find and kill the process |
| `fork() failed` | System process limit hit | Rare — restart the server |
| Client shows `ERR\|UNKNOWN_COMMAND` | Malformed request string | Check client/server versions match |
| Tailscale ping fails | Machines on different accounts | Both machines must log into the same Tailscale account |
| Data corruption after crash | Server was killed mid-write | Restore `voters.dat` / `candidates.dat` from backup |

**Test the tunnel before running the app:**
```bash
# From client machine
tailscale ping <server-tailscale-ip>

# Manually test the UDP connection
echo "PING" | nc -u <server-ip> 8080
```

**Check server logs** — the server prints every datagram it receives and every response it sends, including the child PID:

```
[SERVER] Datagram from 100.64.1.5:51234  ->  LOGIN_VOTER|alice|pass123
[CHILD 3842] Processing: LOGIN_VOTER|alice|pass123
[CHILD 3842] Sending  : OK|1|Alice Mwangi
[SERVER] Forked child PID 3842 — back to listening.
```