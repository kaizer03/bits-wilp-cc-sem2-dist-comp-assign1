# Distributed File Server System — Assignment I

**BITS Pilani WILP | Cloud Computing | CISS Assignment 1**

---

## Table of Contents

1. [Assignment Objective](#assignment-objective)
2. [System Architecture](#system-architecture)
3. [Use Cases & Expected Behaviour](#use-cases--expected-behaviour)
4. [Test Data Setup](#test-data-setup)
5. [Code Walkthrough](#code-walkthrough)
   - [Shared Protocol Primitives](#shared-protocol-primitives)
   - [server2.c — Replica File Server](#server2c--replica-file-server)
   - [server1.c — Primary File Server](#server1c--primary-file-server)
   - [client.c — File Requester](#clientc--file-requester)
6. [Building the Project](#building-the-project)
7. [Running the System](#running-the-system)
8. [Walkthrough of All Client Commands](#walkthrough-of-all-client-commands)

---

## Assignment Objective

The assignment provides a hands-on understanding of the **client–server paradigm in distributed systems**. The goal is to implement a simple distributed system comprising one client and two file servers, where the two servers act as replicas of each other (with the possibility of replication lag, meaning files may differ between servers at a given point in time).

The system must correctly handle every possible state of file availability across the two servers and communicate the appropriate response back to the requesting client.

---

## System Architecture

```
  ┌────────┐          ┌───────────┐          ┌───────────┐
  │ CLIENT │ ───────► │  SERVER1  │ ───────► │  SERVER2  │
  │        │          │  :5001    │          │  :5002    │
  │        │ ◄─────── │  primary  │ ◄─────── │  replica  │
  └────────┘  result  └───────────┘  result  └───────────┘
                           │                      │
                    ./server1_files/        ./server2_files/
```

**Components:**

| Component | Role |
|-----------|------|
| `CLIENT`  | Requests a file by pathname from SERVER1 and saves whatever it receives to disk. |
| `SERVER1` | Primary server. Checks its own filesystem, queries SERVER2 in parallel, compares results, and responds to CLIENT. |
| `SERVER2` | Replica server. Responds only to SERVER1 with a file or a not-found status. |

**Communication:** All three components communicate over **TCP sockets** using a compact binary protocol with network byte order (big-endian) length-prefixed messages.

---

## Use Cases & Expected Behaviour

The system is designed to handle five distinct scenarios:

### Case 1 — File identical on both servers (`a.txt`)
- Both SERVER1 and SERVER2 have the file with exactly the same content.
- SERVER1 compares the two copies using a byte-for-byte comparison.
- **Result:** CLIENT receives **one file**, saved as `received_a.txt`.

### Case 2 — File different on both servers (`b.txt`)
- Both SERVER1 and SERVER2 have the file but with different content (simulating replication lag or divergence).
- SERVER1 detects the mismatch and sends both copies.
- **Result:** CLIENT receives **two files**, saved as `received1_b.txt` (SERVER1's copy) and `received2_b.txt` (SERVER2's copy).

### Case 3 — File only on SERVER1 (`c.txt`)
- SERVER1 has the file; SERVER2 does not.
- SERVER1 serves its own copy directly.
- **Result:** CLIENT receives **one file**, saved as `received_c.txt`.

### Case 4 — File only on SERVER2 (`d.txt`)
- SERVER2 has the file; SERVER1 does not.
- SERVER1 forwards SERVER2's copy to CLIENT.
- **Result:** CLIENT receives **one file**, saved as `received_d.txt`.

### Case 5 — File not on any server (`nope.txt`)
- Neither server has the file.
- SERVER1 sends a NOT FOUND response.
- **Result:** CLIENT prints a not-found message and saves nothing.

---

## Test Data Setup

The project ships with pre-populated test directories to exercise all five use cases:

```
bits-wilp-cc-sem2-dist-comp-assign1/
├── server1_files/
│   ├── a.txt   → "hello"           (same as server2/a.txt  → Case 1: identical)
│   ├── b.txt   → "server1 version" (differs from server2   → Case 2: different)
│   └── c.txt   → "only on server1" (absent from server2    → Case 3: S1 only)
│
└── server2_files/
    ├── a.txt   → "hello"           (same as server1/a.txt  → Case 1: identical)
    ├── b.txt   → "server2 version" (differs from server1   → Case 2: different)
    └── d.txt   → "only on server2" (absent from server1    → Case 4: S2 only)
```

`nope.txt` is absent from both directories, covering Case 5.

---

## Code Walkthrough

### Shared Protocol Primitives

All three source files define two low-level socket helpers that guarantee complete transmission over TCP (which may deliver data in fragments):

#### `recv_all(sock, buf, n)`
Loops calling `recv()` until exactly `n` bytes have been read into `buf`. Returns `-1` on connection close or error, `0` on success. Handles `EINTR` (signal interruptions) transparently.

#### `send_all(sock, buf, n)`
Loops calling `send()` until exactly `n` bytes from `buf` have been written to the socket. Handles `EINTR` transparently.

These helpers are the foundation of the **length-prefixed binary protocol** used throughout the system.

---

### `server2.c` — Replica File Server

**Role:** Passive replica. Receives file requests exclusively from SERVER1 and responds with either the file content or a not-found status.

**Startup:** `./bin/server2 <port> <base_dir>`

**Main loop:**
1. Binds to a TCP port and enters an `accept()` loop.
2. For each incoming connection (always from SERVER1):
   - **Receive pathname** via `recv_pathname()`: reads a 4-byte big-endian length followed by that many bytes of filename string.
   - **Validate path** via `build_fullpath()`: blocks absolute paths (starting with `/`) and directory traversal attempts (`..`) to prevent path-injection attacks.
   - **Read file** via `read_file_to_buf()`: opens the file, uses `fstat()` to get its size, allocates a heap buffer, and reads the entire file contents into memory.
   - **Send reply:**
     - Not found → sends `status=0` (4 bytes).
     - Found → sends `status=1` + `file_len` (4 bytes) + raw file bytes.

**Wire format (SERVER2 reply):**
```
[uint32 status] [uint32 file_len] [file_len bytes of data]
   (0=not found)   (only if status=1)
```

---

### `server1.c` — Primary File Server

**Role:** Orchestrator. Serves CLIENT requests by consulting both its own local filesystem and SERVER2, then applying the assignment's decision logic before responding.

**Startup:** `./bin/server1 <listen_port> <server2_ip> <server2_port> <base_dir>`

**Main loop (per client connection):**

1. **Accept** a TCP connection from CLIENT.
2. **Receive pathname** from CLIENT (same length-prefixed format).
3. **Check local filesystem (SERVER1):** calls `build_fullpath()` + `read_file_to_buf()`. Sets `found1` flag and stores content in `buf1`.
4. **Query SERVER2:** opens a fresh TCP connection to SERVER2, sends the same pathname, reads the reply via `recv_server2_reply()`. Sets `found2` flag and stores content in `buf2`. If SERVER2 is unreachable, SERVER1 gracefully falls back to serving only its local copy.
5. **Decision logic** (core of the assignment):

   | `found1` | `found2` | Action |
   |----------|----------|--------|
   | No       | No       | Send `status=0` (NOT FOUND) to CLIENT |
   | Yes      | No       | Send `status=1` + `buf1` to CLIENT |
   | No       | Yes      | Send `status=1` + `buf2` to CLIENT |
   | Yes      | Yes, identical  | Send `status=1` + `buf1` to CLIENT |
   | Yes      | Yes, different  | Send `status=2` + `buf1` + `buf2` to CLIENT |

6. **Free** both buffers and close the client connection.

**Wire format (SERVER1 → CLIENT reply):**
```
Status 0 (not found):
  [uint32: 0]

Status 1 (one file):
  [uint32: 1] [uint32: file_len] [file_len bytes]

Status 2 (two divergent files):
  [uint32: 2] [uint32: len1] [len1 bytes] [uint32: len2] [len2 bytes]
```

**Comparison:** `buffers_equal()` does a length check followed by `memcmp()` — a byte-perfect comparison of the entire file contents.

---

### `client.c` — File Requester

**Role:** Initiates a single file request to SERVER1 and saves received data to disk.

**Usage:** `./bin/client <server1_ip> <server1_port> <pathname>`

**Flow:**

1. **Connect** to SERVER1 via TCP.
2. **Send pathname** via `send_pathname()`: sends 4-byte length + filename bytes.
3. **Receive status** (4 bytes, big-endian).
4. **Branch on status:**
   - `0` → Print "NOT FOUND", exit cleanly.
   - `1` → Receive `[uint32 len][len bytes]`, write to `received_<pathname>`.
   - `2` → Receive two length-prefixed blobs, write to `received1_<pathname>` and `received2_<pathname>`.
5. **Path sanitisation** via `sanitize_path()`: replaces `/` and `\` in the pathname with `_` when constructing output filenames, preventing accidental writes to subdirectories.

---

## Building the Project

The project uses a **Makefile** (the C-project equivalent of `package.json`) to manage all build commands. `gcc` with `-O2 -Wall -Wextra` flags is used for optimised, warning-clean compilation.

**Build everything at once:**
```bash
make
```

**Build individual targets:**
```bash
make server2
make server1
make client
```

**Clean compiled binaries:**
```bash
make clean
```

All binaries are output to the `bin/` directory.

---

## Running the System

The three processes must be started in order: SERVER2 first, then SERVER1, then CLIENT.

### Step 1 — Start SERVER2 (Replica)

Open a dedicated terminal:
```bash
cd ~/bits-wilp-cc-sem2-dist-comp-assign1
./bin/server2 5002 ./server2_files
```
SERVER2 will print `[SERVER2] Listening on port 5002, base_dir=./server2_files` and wait for connections.

### Step 2 — Start SERVER1 (Primary)

Open a second terminal:
```bash
cd ~/bits-wilp-cc-sem2-dist-comp-assign1
./bin/server1 5001 127.0.0.1 5002 ./server1_files
```
SERVER1 will print `[SERVER1] Listening on port 5001, ...` and wait for client connections.

### Step 3 — Run CLIENT Commands

Open a third terminal and run any of the following commands:

---

## Walkthrough of All Client Commands

### `./bin/client 127.0.0.1 5001 a.txt`
**Scenario:** `a.txt` exists on both servers with identical content (`"hello"`).
```
[CLIENT] Received ONE file (6 bytes). Saved as 'received_a.txt'
```
SERVER1 finds both copies identical and sends a single file.

---

### `./bin/client 127.0.0.1 5001 b.txt`
**Scenario:** `b.txt` exists on both servers but with different content (`"server1 version"` vs `"server2 version"`).
```
[CLIENT] Received TWO files (different copies).
         File1: 16 bytes -> 'received1_b.txt'
         File2: 16 bytes -> 'received2_b.txt'
```
SERVER1 detects the mismatch and sends both divergent copies so the client can inspect the difference.

---

### `./bin/client 127.0.0.1 5001 c.txt`
**Scenario:** `c.txt` exists only on SERVER1 (`"only on server1"`). SERVER2 reports not found.
```
[CLIENT] Received ONE file (16 bytes). Saved as 'received_c.txt'
```
SERVER1 serves its own copy directly.

---

### `./bin/client 127.0.0.1 5001 d.txt`
**Scenario:** `d.txt` exists only on SERVER2 (`"only on server2"`). SERVER1 does not have it locally.
```
[CLIENT] Received ONE file (16 bytes). Saved as 'received_d.txt'
```
SERVER1 proxies SERVER2's copy to the client.

---

### `./bin/client 127.0.0.1 5001 nope.txt`
**Scenario:** `nope.txt` does not exist on either server.
```
[CLIENT] File 'nope.txt' NOT FOUND on servers.
```
SERVER1 responds with a not-found status. No file is written to disk.

---

## Summary of Output Files

After running all five client commands, the following files will appear in your working directory:

| Output File         | Contents                        |
|---------------------|---------------------------------|
| `received_a.txt`    | `hello` (single agreed copy)    |
| `received1_b.txt`   | `server1 version` (S1's copy)   |
| `received2_b.txt`   | `server2 version` (S2's copy)   |
| `received_c.txt`    | `only on server1`               |
| `received_d.txt`    | `only on server2`               |
