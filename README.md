# SONU Electronic Voting System — Assignment 2

## Architecture


This is the **client-server** implementation of the SONU Electronic Voting System. Both the server and client run on the **same machine** (localhost) and communicate over **UDP sockets** (connectionless) using a **pipe-delimited** application protocol.

**Note:**
- The server is **iterative**: it processes one request at a time, in a loop (no concurrency).
- The server is **connectionless**: it uses UDP sockets (`recvfrom`/`sendto`), so there is no persistent connection between client and server.

| Component | Description |
|-----------|-------------|
| **Server** (`bin/server`) | Iterative, connectionless UDP server bound to `127.0.0.1:8080`. Processes one client request at a time. Contains all business logic and file database access. |
| **Client** (`bin/client`) | Menu-driven terminal UI. Sends a UDP datagram to the server for each operation. Contains no business logic — pure presentation + networking. |

## Building

```bash
make
```

This produces two executables: `bin/server` and `bin/client` (Linux-style, no `.exe` extension).

## Running

1. **Start the server** in one terminal:

   ```bash
   ./bin/server
   ```

2. **Start the client** in another terminal:

   ```bash
   ./bin/client
   ```

3. Use the client menu to register voters/candidates, login, vote, and view results. All requests are sent to the server via UDP (connectionless, one datagram per request).

## Application Protocol

Commands are pipe-delimited strings terminated by `\n`.

| Feature | Client → Server | Server → Client |
|---------|-----------------|-----------------|
| Register Voter | `REG_VOTER\|name\|user\|pass` | `OK\|voter_id` or `ERR\|USERNAME_TAKEN` |
| Register Candidate | `REG_CAND\|name\|user\|pass\|pos_id` | `OK\|cand_id` or `ERR\|...` |
| Login Voter | `LOGIN_VOTER\|user\|pass` | `OK\|voter_id\|full_name` or `ERR\|INVALID_CREDENTIALS` |
| Login Candidate | `LOGIN_CAND\|user\|pass` | `OK\|cand_id\|name\|position\|votes` or `ERR\|...` |
| Cast Vote | `CAST_VOTE\|voter_id\|cand_id\|pos_id` | `OK\|VOTE_CAST` or `ERR\|...` |
| Get Results | `GET_RESULTS\|pos_id` | `OK\|count\|name1\|votes1\|...` or `ERR\|INVALID_POSITION` |
| List Candidates | `LIST_CANDS\|pos_id` | `OK\|count\|id1\|name1\|votes1\|...` |
| Voter Status | `VOTER_STATUS\|voter_id` | `OK\|name\|flag0\|flag1\|...\|flag4` |

## Data Files

- `voters.dat` — Binary file of fixed-size `Voter` structs
- `candidates.dat` — Binary file of fixed-size `Candidate` structs

Both files are created automatically on first use and are accessed
only by the server.

## Module Structure

```
assignment2/
├── server.c        ← TCP server + command dispatcher
├── client.c        ← TCP client + menu UI
├── voter.c/h       ← Voter registration, login, status (server-side)
├── candidate.c/h   ← Candidate registration, login, lookup (server-side)
├── election.c/h    ← Vote casting + result tallying (server-side)
├── file_io.c/h     ← Binary file helpers (server-side)
├── positions.h     ← Compile-time position constants (shared)
└── Makefile
```

## Conceptual Server Algorithm by Program

Each program below is accompanied by the conceptual server algorithm as it is
applied in that specific module.

### 1) `server.c` — Core Request/Response Server Algorithm

1. Create a UDP socket and bind to `127.0.0.1:8080`.
2. Enter infinite iterative loop:
   - Wait for a datagram from any client using `recvfrom`.
   - Parse the received command using `|` delimiters.
   - Dispatch to the matching business operation.
   - Build protocol response (`OK|...` or `ERR|...`).
   - Send response to the client using `sendto`.
3. Repeat for next request.

This is an **iterative** (one-request-at-a-time) and **connectionless** (UDP) server implementation.

### 2) `client.c` — Client-Side Application of the Server Algorithm

For each menu action, the client applies a request cycle compatible with the
server algorithm:

1. Collect user input from terminal UI.
2. Encode request as a pipe-delimited command (`COMMAND|arg1|arg2...\n`).
3. Open a fresh TCP connection to server.
4. Send one command.
5. Receive one response.
6. Parse `OK/ERR` fields and render user-facing output.
7. Close socket.

So the client mirrors the server's one-request-per-connection processing model.

### 3) `voter.c` — Voter Sub-Algorithm (Server Business Layer)

#### Register Voter
1. Check username uniqueness by scanning `voters.dat`.
2. Generate `voter_id = record_count + 1`.
3. Initialize voter record (registered flag, vote flags = 0).
4. Append record to file.
5. Return new voter id or error.

#### Login Voter
1. Find voter by username.
2. Compare plaintext password.
3. Return voter id on match, else invalid credentials.

#### Mark Position Voted
1. Seek directly to voter offset `(voter_id-1)*sizeof(Voter)`.
2. Load record and set `voted_positions[position_id] = 1`.
3. Seek back and overwrite record.

### 4) `candidate.c` — Candidate Sub-Algorithm (Server Business Layer)

#### Register Candidate
1. Validate `position_id` range.
2. Check username uniqueness in `candidates.dat`.
3. Generate `candidate_id = record_count + 1`.
4. Initialize record (`vote_count = 0`, registered flag).
5. Append to file and return id.

#### Login Candidate
1. Scan candidate records by username.
2. Validate password.
3. Return candidate id on success.

#### Candidate Lookup + Vote Increment
1. Seek candidate by direct offset.
2. Validate active record and (when voting) position match.
3. Increment `vote_count` and overwrite same record.

### 5) `election.c` — Vote Casting and Results Sub-Algorithm

#### Cast Vote
1. Validate requested position range.
2. Fetch voter; ensure voter exists and is registered.
3. Check voter has not voted for that position.
4. Fetch candidate; ensure candidate exists and belongs to same position.
5. Increment candidate vote count.
6. Mark voter as voted for that position.
7. Return vote status code (`VOTE_OK` or specific error).

#### Generate Results
1. Retrieve all candidates for a position.
2. Sort candidates descending by vote count.
3. Return sorted list for server response formatting.

### 6) `file_io.c` — File Access Support Sub-Algorithm

1. Open requested data file in caller-specified mode.
2. For record counting, open file in binary read mode.
3. Seek to end and compute `bytes / record_size`.
4. Return count (or `0` if file does not yet exist).

This module abstracts storage primitives used by all server business modules.
