<<<<<<< HEAD
# SONU Electronic Voting System — Assignment 2
=======
# SONU Electronic Voting System — Assignment 3

## Overview


This is the **Assignment 3** upgrade of the SONU Electronic Voting System.
It implements a **Linux/POSIX, connectionless UDP client-server architecture**. The server handles each UDP datagram request in a separate child process (via `fork()`), allowing multiple clients to be served concurrently. The client and server communicate using a simple, pipe-delimited text protocol. All business logic and data storage are handled by the server; the client is stateless and connectionless.


| What changed | Assignment 2 | Assignment 3 |
|---|---|---|
| OS / API | Windows (Winsock2) | Linux / POSIX |
| Transport | TCP — `SOCK_STREAM` | **UDP — `SOCK_DGRAM`** |
| Concurrency | Iterative (one client at a time) | **Concurrent: fork() per datagram** |
| Connection model | Connection-oriented (`connect` / `accept`) | **Connectionless** (`sendto` / `recvfrom`) |
| Client IP | Hardcoded `127.0.0.1` | **Runtime argument** `./client [ip]` |

---
>>>>>>> 2ba28a9 (update readme.md)


## Directory Structure & Architecture

```
assignment3/
├── src/
│   ├── server.c        ← UDP server + fork() dispatcher
│   ├── client.c        ← UDP client + menu UI
│   ├── voter.c         ← Voter registration, login, status (server-side)
│   ├── candidate.c     ← Candidate registration, login, lookup (server-side)
│   ├── election.c      ← Vote casting + result tallying (server-side)
│   └── file_io.c       ← Binary flat-file helpers (server-side)
├── headers/
│   ├── positions.h     ← Compile-time position constants (shared)
│   ├── voter.h
│   ├── candidate.h
│   ├── election.h
│   └── file_io.h
├── data/
│   ├── voters.dat      ← Binary flat file of Voter structs
│   └── candidates.dat  ← Binary flat file of Candidate structs
└── Makefile
```

### Component roles

| Component | Description |
|---|---|
| `server.c` | UDP server bound to `0.0.0.0:8080`. Listens on all network interfaces. For each incoming datagram it `fork()`s a child process that handles the command and replies, while the parent immediately loops back to `recvfrom()`. Contains all business logic dispatch. |
| `client.c` | Menu-driven terminal UI. For each operation it opens a fresh UDP socket, sends one datagram to the server, waits up to 5 seconds for the reply, then closes the socket. Accepts the server IP as an optional command-line argument. |
| `voter.c` | Voter registration, login, record lookup, and voted-position marking. All file I/O on `voters.dat`. |
| `candidate.c` | Candidate registration, login, lookup by ID, and vote-count increment. All file I/O on `candidates.dat`. |
| `election.c` | `cast_vote()` (two-stage security check + atomic write) and `generate_results()` (scan + descending sort). |
| `file_io.c` | `open_voters_file()`, `open_candidates_file()`, `get_record_count()`. Centralises file paths. |

---


## Building

Requires GCC and standard POSIX libraries (no extra packages needed on any modern Linux distribution).

```bash
make
```


This produces `bin/server` and `bin/client` (executables for Linux, no .exe extension).

To rebuild from scratch:

```bash
make clean && make
```

---


## Running


### Same machine (localhost)

```bash
# Terminal 1 — start the server
./bin/server

# Terminal 2 — start the client (defaults to 127.0.0.1)
./bin/client
```


### Same network (LAN)

The server prints its local IP addresses at startup:

```
[SERVER] Local interfaces:
         lo            127.0.0.1
         eth0          192.168.1.42
```

On any other machine on the same network:

```bash
./bin/client 192.168.1.42
```


### Different network (across the internet)

Two steps are required on the **server side**:

1. **Open the firewall** for UDP port 8080:
   ```bash
   sudo ufw allow 8080/udp
   ```

2. **Set up port forwarding** on the server's router:
   - Log into the router admin panel (usually `192.168.1.1`).
   - Add a rule: Protocol = UDP, External port = 8080,
     Internal IP = server's LAN IP, Internal port = 8080.

Find the server's public IP:
```bash
curl ifconfig.me
```

On the remote client machine:
```bash
./bin/client 203.0.113.5      # replace with actual public IP
```

The client banner confirms the target on startup:
```
==========================================
    ELECTRONIC VOTING SYSTEM  v3.0
    Assignment 3  —  UDP Connectionless
==========================================
    Server  : 203.0.113.5:8080
==========================================
```

---


## Application Protocol

All messages are **pipe-delimited plain-text strings terminated by `\n`**.
Each client operation is exactly one datagram sent and one datagram received —
no session state is maintained between operations.


| Operation | Client → Server | Server → Client |
|---|---|---|
| Register voter | `REG_VOTER|name|user|pass` | `OK|voter_id` or `ERR|USERNAME_TAKEN` |
| Register candidate | `REG_CAND|name|user|pass|pos_id` | `OK|cand_id` or `ERR|INVALID_POSITION` / `ERR|USERNAME_TAKEN` |
| Login voter | `LOGIN_VOTER|user|pass` | `OK|voter_id|full_name` or `ERR|INVALID_CREDENTIALS` |
| Login candidate | `LOGIN_CAND|user|pass` | `OK|cand_id|name|position|votes` or `ERR|INVALID_CREDENTIALS` |
| Cast vote | `CAST_VOTE|voter_id|cand_id|pos_id` | `OK|VOTE_CAST` or `ERR|ALREADY_VOTED` / `ERR|INVALID_CANDIDATE` / `ERR|VOTER_NOT_FOUND` |
| Get results | `GET_RESULTS|pos_id` | `OK|count|name1|votes1|name2|votes2|...` or `ERR|INVALID_POSITION` |
| List candidates | `LIST_CANDS|pos_id` | `OK|count|id1|name1|votes1|...` or `ERR|INVALID_POSITION` |
| Voter status | `VOTER_STATUS|voter_id` | `OK|name|flag0|flag1|flag2|flag3|flag4` (flag = 1 if voted) |

### Position IDs

| ID | Position |
|---|---|
| 0 | Chairman |
| 1 | Vice Chairman |
| 2 | Secretary |
| 3 | Treasurer |
| 4 | PRO |

---

## Server Algorithm (step by step)

### Startup

1. Set `SIGCHLD = SIG_IGN` — kernel auto-reaps child processes, preventing zombie accumulation.
2. Create a UDP socket: `socket(AF_INET, SOCK_DGRAM, 0)`.
3. Set `SO_REUSEADDR` so the port can be rebound immediately after a restart.
4. Bind to `INADDR_ANY:8080` — listens on all network interfaces (localhost, LAN, and any external interface).
5. Print all local IPv4 addresses using `getifaddrs()`.
6. Enter the main receive loop.

### Main receive loop (iterative + concurrent)

```
loop forever:
    recvfrom()          ← blocks until a datagram arrives
                          fills buffer with payload
                          fills client_addr with sender's IP + port

    fork()
    ├─ CHILD (pid == 0):
    │     handle_command(buffer → response)
    │     sendto(server_sock, response, client_addr)
    │     close(server_sock)
    │     exit(0)
    │
    └─ PARENT (pid > 0):
          log "Forked child PID n"
          loop back to recvfrom() immediately
```

The server is **iterative** because there is a single listening socket and no
`accept()`. It is **concurrent** because every datagram is handled by its own
child process, so multiple clients can be served simultaneously.

### handle_command() dispatch

1. Copy raw buffer, strip `\r\n`.
2. Split on `|` into field array `f[]`.
3. Switch on `f[0]` (the command name).
4. Call the appropriate business-logic function from `voter.c`, `candidate.c`,
   or `election.c`.
5. Write the `OK|...` or `ERR|...` response into the response buffer.

---

## Client Algorithm (step by step)

### Startup

1. If `argv[1]` is provided, copy it into `g_server_ip`; otherwise keep the
   default `"127.0.0.1"`.
2. Print the banner showing the target server IP and port.
3. Enter the main menu loop.

### send_command() — one UDP round-trip per operation

```
1. socket(AF_INET, SOCK_DGRAM, 0)
2. setsockopt SO_RCVTIMEO = 5 seconds   ← prevents infinite blocking
3. sendto(sock, command, server_addr)
4. recvfrom(sock, response)             ← waits up to 5 s for reply
5. close(sock)
6. return parsed response to caller
```

A fresh socket is created for every command. There is no persistent
connection — each menu action is a self-contained datagram exchange.

---

## Business Logic Algorithms

### voter.c

#### Register voter
1. Scan `voters.dat` for an existing record with the same username.
2. If duplicate found, return `-1`.
3. Compute `voter_id = get_record_count() + 1`.
4. Initialise struct: `voted_positions[0..4] = 0`, `is_registered = 1`.
5. Append struct to file. Return `voter_id`.

#### Login voter
1. Scan `voters.dat` for a matching `username` + `password` pair.
2. Return `voter_id` on success, `-1` on failure.

#### Mark position voted
1. Direct-seek to `(voter_id - 1) * sizeof(Voter)`.
2. Read record, set `voted_positions[position_id] = 1`.
3. Seek back and overwrite the record in place.

### candidate.c

#### Register candidate
1. Validate `position_id` is in range `[0, MAX_POSITIONS)` — return `-2` if not.
2. Scan `candidates.dat` for username uniqueness — return `-1` if taken.
3. Compute `candidate_id = get_record_count() + 1`.
4. Initialise struct: `vote_count = 0`, `is_registered = 1`.
5. Append and return `candidate_id`.

#### Login candidate
1. Scan `candidates.dat` for matching `username` + `password`.
2. Return `candidate_id` on success, `-1` on failure.

#### Increment vote count
1. Direct-seek to `(candidate_id - 1) * sizeof(Candidate)`.
2. Read record, increment `vote_count`.
3. Seek back and overwrite.

### election.c

#### cast_vote (two security checks before any write)

**Security check 1 — voter eligibility:**
- Load voter by ID. Verify `is_registered == 1`.
- Verify `voted_positions[position_id] == 0` (not already voted here).
- On failure return `VOTE_ERR_NOT_FOUND` or `VOTE_ERR_ALREADY`.

**Security check 2 — candidate validity:**
- Load candidate by ID. Verify `is_registered == 1`.
- Verify `candidate.position_id == position_id` (prevents cross-position fraud).
- On failure return `VOTE_ERR_INVALID`.

**Commit (only if both checks pass):**
1. `increment_vote_count(candidate_id)`.
2. `mark_position_voted(voter_id, position_id)`.
3. Return `VOTE_OK`.

#### generate_results
1. Scan `candidates.dat`, collect all active records for `position_id`.
2. Bubble-sort descending by `vote_count`.
3. Return sorted array and count.

### file_io.c

- `open_voters_file(mode)` — `fopen(VOTERS_FILE, mode)`.
- `open_candidates_file(mode)` — `fopen(CANDIDATES_FILE, mode)`.
- `get_record_count(filename, record_size)` — seek to end, divide byte count
  by `record_size`. Returns `0` if file does not exist.

---


## Data Files

Both files live in the `data/` directory and are created automatically on first use. They store fixed-size C structs sequentially — record `n` is always at byte offset `(n-1) * sizeof(struct)`, enabling O(1) direct-seek access.

| File | Struct | Key fields |
|---|---|---|
| `data/voters.dat` | `Voter` | `voter_id`, `username`, `password`, `voted_positions[5]`, `is_registered` |
| `data/candidates.dat` | `Candidate` | `candidate_id`, `username`, `password`, `position_id`, `vote_count`, `is_registered` |


> **Note:** The data files are shared exclusively by the server. The client never touches them directly — all data access goes through the UDP protocol.

---


## Firewall & Port Forwarding Quick Reference

| Scenario | What to do |
|---|---|
| Same machine | Nothing — `127.0.0.1` works out of the box |
| Same LAN | Open firewall: `sudo ufw allow 8080/udp` |
| Different network | Firewall + port forwarding on router (UDP 8080 → server LAN IP) |
| No router access | Use a cloud VM with a public IP, or a tunnel (e.g. WireGuard) |


Find the server's public IP (needed for cross-network clients):
```bash
curl ifconfig.me
```