# Distributed File System — Assignment I

**BITS Pilani WILP | Cloud Computing | CISS Assignment 1**

---

## Table of Contents

1. [Assignment Objective](#assignment-objective)
2. [System Architecture](#system-architecture)
3. [Communication Protocol](#communication-protocol)
4. [File & Directory Layout](#file--directory-layout)
5. [Building the Project](#building-the-project)
6. [Running the System](#running-the-system)
   - [The Easy Way — `make run`](#the-easy-way--make-run)
   - [The Manual Way](#the-manual-way)
7. [Interactive Client — Full Reference](#interactive-client--full-reference)
   - [Startup Flow](#startup-flow)
   - [Main Menu](#main-menu)
   - [a. Download File](#a-download-file)
   - [b. File Size](#b-file-size)
   - [c. Create File](#c-create-file)
   - [d. List Files](#d-list-files)
   - [e. View File](#e-view-file)
   - [f. Delete File](#f-delete-file)
   - [g. Exit](#g-exit)
   - [The `!cancel` Escape Token](#the-cancel-escape-token)
8. [Server Auto-Recovery — All Scenarios](#server-auto-recovery--all-scenarios)
   - [How Recovery is Triggered](#how-recovery-is-triggered)
   - [Scenario 1 — Server 2 Goes Offline](#scenario-1--server-2-goes-offline)
   - [Scenario 2 — Server 1 Goes Offline (Server 2 Still Running)](#scenario-2--server-1-goes-offline-server-2-still-running)
   - [Scenario 3 — Both Servers Go Offline](#scenario-3--both-servers-go-offline)
   - [Recovery During Mid-Request Disconnect](#recovery-during-mid-request-disconnect)
   - [Recovery Timeout](#recovery-timeout)
9. [File Distribution Use Cases](#file-distribution-use-cases)
10. [Test Data Setup](#test-data-setup)
11. [Unit Testing](#unit-testing)
    - [Running the Tests](#running-the-tests)
    - [Test Suites](#test-suites)
12. [Code Walkthrough](#code-walkthrough)
    - [Shared Protocol Primitives](#shared-protocol-primitives)
    - [server2.c — Replica File Server](#server2c--replica-file-server)
    - [server1.c — Primary File Server / Orchestrator](#server1c--primary-file-server--orchestrator)
    - [client.c — Interactive Terminal Application](#clientc--interactive-terminal-application)
13. [Platform Support](#platform-support)
14. [Runtime Terminal Walkthrough](#runtime-terminal-walkthrough)

---

## Assignment Objective

The assignment provides a hands-on understanding of the **client–server paradigm in distributed systems**. The goal is to implement a simple distributed system comprising one client and two file servers that act as replicas of each other (with the possibility of replication lag — files may differ between servers at any given point in time).

The system must correctly handle every possible state of file availability across the two servers and communicate the appropriate response back to the requesting client.

Beyond the core assignment, the system has been developed into a **fully interactive terminal application** where:

- The client is a rich, menu-driven interactive program.
- Server processes are launched **automatically** by the client in new terminal windows.
- If either server goes offline mid-operation, the client **auto-recovers** — relaunches the affected server(s), waits for them to return online, and resumes the interrupted request without requiring any user input.
- All server-to-server communication follows a strict **Client → Server1 → Server2** hierarchy. The client never contacts Server 2 directly.

---

## System Architecture

```
  ┌──────────────┐          ┌───────────────────┐          ┌───────────────────┐
  │    CLIENT    │ ───────► │     SERVER 1      │ ───────► │     SERVER 2      │
  │  (port agn.) │          │     :5001         │          │     :5002         │
  │  interactive │ ◄─────── │  primary/orchest. │ ◄─────── │  replica/passive  │
  └──────────────┘  result  └───────────────────┘  result  └───────────────────┘
                                    │                              │
                             ./server1_files/              ./server2_files/
```

**Rule: The client never contacts Server 2 directly. Every operation — including health checks — flows Client → Server 1 → Server 2.**

| Component | Role |
|-----------|------|
| `CLIENT`  | Interactive menu-driven application. Sends all requests to Server 1 only. Saves received files into `./client_files/`. |
| `SERVER 1` | Primary server and orchestrator. Handles all client requests, queries Server 2 as needed, applies decision logic, and responds to the client. |
| `SERVER 2` | Passive replica. Only accepts connections from Server 1. Never contacted directly by the client. |

---

## Communication Protocol

All components communicate over **TCP sockets** using a compact binary protocol with **network byte order (big-endian)** length-prefixed messages.

### Opcodes

Each client request to Server 1 begins with a single-byte opcode:

| Opcode | Hex  | Operation |
|--------|------|-----------|
| PING   | 0x01 | Health check — Server 1 pings Server 2 and returns combined status |
| RETRIEVE | 0x02 | Download a named file |
| SIZE   | 0x03 | Get the size of a named file on each server |
| CREATE | 0x04 | Create a named file (with optional content) on one or both servers |
| LIST   | 0x05 | List all files from one or both servers |
| DELETE | 0x06 | Delete a named file from one or both servers |
| SHUTDOWN | 0x07 | Gracefully shut down Server 1 and Server 2 |

### Target Values (CREATE, LIST, DELETE)

| Value | Meaning |
|-------|---------|
| 0 | Both servers |
| 1 | Server 1 only |
| 2 | Server 2 only |

### Wire Format

```
Request to Server 1:
  [uint8 opcode] [payload...]

String field (filename etc.):
  [uint32 length][length bytes of UTF-8 string]

RETRIEVE response (Server 1 → Client):
  Status 0 (not found):    [uint32: 0]
  Status 1 (single copy):  [uint32: 1][uint32: len][len bytes]
  Status 2 (two divergent copies): [uint32: 2][uint32: len1][len1 bytes][uint32: len2][len2 bytes]

PING response (Server 1 → Client):
  [uint32: s1_up=1][uint32: s2_up]

SIZE response (Server 1 → Client):
  [uint32: s1_found][uint32: s1_size][uint32: s2_found][uint32: s2_size]

CREATE response (Server 1 → Client):
  [uint32: s1_status][uint32: s2_status]   (0=skipped, 1=ok, 2=error)

LIST response (Server 1 → Client):
  [uint32: s1_count] for each: [uint32: namelen][name bytes][uint32: size]
  [uint32: s2_count] for each: [uint32: namelen][name bytes][uint32: size]

DELETE — Phase 1, Server 1 → Client (existence check):
  [uint32: s1_found][uint32: s2_found]
DELETE — Phase 2, Client → Server 1 (delete target):
  [uint8: del_target]
DELETE — Phase 2 response, Server 1 → Client:
  [uint32: s1_del][uint32: s2_del]   (0=not found, 1=deleted, 2=error, 3=skipped)
```

---

## File & Directory Layout

```
bits-wilp-cc-sem2-dist-comp-assign1/
├── client.c                   ← Interactive client source
├── server1.c                  ← Primary server source
├── server2.c                  ← Replica server source
├── Makefile
├── README.md
├── CONTRIBUTIONS.md
├── .clangd                    ← clangd include-path config (IDE support)
├── .vscode/
│   └── c_cpp_properties.json  ← VS Code / Cursor C/C++ IntelliSense config
├── bin/                       ← Compiled binaries (created by make)
│   ├── client
│   ├── server1
│   └── server2
├── client_files/              ← Downloaded files are saved here
├── server1_files/             ← Server 1's file storage
│   ├── a.txt
│   ├── b.txt
│   └── test.txt
├── server2_files/             ← Server 2's file storage
│   ├── a.txt
│   ├── b.txt
│   └── (other files...)
└── test/                      ← Unity unit test suite
    ├── unity.c                ← Unity framework source
    ├── unity.h
    ├── unity_internals.h
    ├── compile_flags.txt      ← clangd flags for test files
    ├── test_pathsanitize.c
    ├── test_sanitize_name.c
    ├── test_cancel_token.c
    ├── test_buffers_equal.c
    ├── test_server_status_code.c
    ├── test_file_identical.c
    ├── test_server_ready.c
    ├── test_recv_length_guard.c
    ├── test_file_size_guard.c
    └── test_target_label.c
```

---

## Building the Project

The project uses a **Makefile** with `gcc -O2 -Wall -Wextra` for optimised, warning-clean compilation.

```bash
# Build all three binaries at once
make

# Build individual targets
make server2
make server1
make client

# Build and run all unit tests
make test

# Clean all compiled binaries and test binaries
make clean

# Build + run the interactive client (also creates client_files/ directory)
make run
```

All binaries are output to the `bin/` directory.

---

## Running the System

### The Easy Way — `make run`

The recommended way to run the entire system is:

```bash
cd /path/to/bits-wilp-cc-sem2-dist-comp-assign1
make run
```

This single command:
1. Builds all three binaries (`server1`, `server2`, `client`).
2. Creates the `client_files/` directory if it doesn't exist.
3. Launches the interactive client in the current terminal.
4. The client will automatically detect your terminal application (iTerm2, Apple Terminal, GNOME Terminal, etc.) and launch Server 1 and Server 2 in **new windows of the same terminal application**.

You do not need to manually start the servers — the client handles all of that.

### The Manual Way

If you prefer full manual control, open three separate terminal windows:

**Terminal 1 — Start Server 2 first (replica):**
```bash
cd /path/to/bits-wilp-cc-sem2-dist-comp-assign1
./bin/server2 5002 ./server2_files
```
Expected output: `[SERVER2] Listening on port 5002, base_dir=./server2_files`

**Terminal 2 — Start Server 1 (primary):**
```bash
cd /path/to/bits-wilp-cc-sem2-dist-comp-assign1
./bin/server1 5001 127.0.0.1 5002 ./server1_files
```
Expected output: `[SERVER1] Listening on port 5001, ...`

**Terminal 3 — Run the interactive client:**
```bash
cd /path/to/bits-wilp-cc-sem2-dist-comp-assign1
./bin/client
# Optional: specify Server 1 address
./bin/client 127.0.0.1 5001
```

---

## Interactive Client — Full Reference

### Startup Flow

When you run the client (via `make run` or `./bin/client`), the following sequence occurs:

```
══════════════════════════════════════════════════════
  Distributed File System Client
  Server 1: 127.0.0.1:5001   Server 2: localhost:5002
══════════════════════════════════════════════════════

Would you like to start the app? [y/n]:
```

**If you type `n`:**
```
Are you sure you want to quit? [y/n]:
```
- `y` → exits the program.
- `n` → returns to the "Would you like to start the app?" prompt.

**If you type `y`:** The client performs a health check by sending `OP_PING` to Server 1, which in turn pings Server 2 and returns the combined status. Four scenarios are possible:

---

**Startup Scenario A — Both servers already running** (e.g. app closed unexpectedly last session):
```
  ⚠  Servers appear to already be running.
  ⚠  The app may have closed unexpectedly last time.
  ✔  Server 1 is already running.
  ✔  Server 2 is already running.
```
The client skips the launch step and proceeds directly to the main menu.

---

**Startup Scenario B — Both servers offline** (fresh start):
```
  Server 1 [:5001]  ○ OFFLINE
  Server 2 [:5002]  ○ OFFLINE

  ℹ  Starting Server 2...
     Launched Server 2 terminal.
  ℹ  Starting Server 1...
     Launched Server 1 terminal.

  ℹ  Starting up the app. Please wait...
     Waiting for servers... (attempt 1/100)  [S1 waiting] [S2 waiting]
     ...
  ✔  Server 1 — ONLINE
  ✔  Server 2 — ONLINE

  App successfully started!
```
Two new terminal windows are opened — one for each server — and the client polls every 3 seconds until both respond.

---

**Startup Scenario C — Server 1 running, Server 2 offline:**
```
  ✔  Server 1 is already running.
  ⚠  Server 2 is offline — launching Server 2 terminal...
     Launched Server 2 terminal.
```
Only a single new terminal for Server 2 is launched.

---

**Startup Scenario D — Server 2 running, Server 1 offline:**

> Note: Because the client can only learn Server 2's status via Server 1, when Server 1 is unreachable during startup the client cannot confirm Server 2's status at that moment. It launches Server 1, waits for Server 1 to come up, and then asks Server 1 about Server 2 before proceeding.

```
  ⚠  Server 1 is offline — launching Server 1 terminal...
     Launched Server 1 terminal.
     Waiting for Server 1 to come online...
  ✔  Server 1 — back ONLINE
  ✔  Server 2 — already ONLINE (confirmed via Server 1)
```

---

After any of the above paths succeed, the **Main Menu** is displayed.

---

### Main Menu

```
══════════════════════════════════════════════════════
  Distributed File System — Main Menu
══════════════════════════════════════════════════════
    a.  Download file
    b.  File size
    c.  Create file
    d.  List files
    e.  View file
    f.  Delete file
    g.  Exit

Your choice:
```

Type a single letter and press Enter. Every option (except `g. Exit`) performs a **background server health check** before executing. If a server is found to be offline at that point, the [auto-recovery](#server-auto-recovery--all-scenarios) mechanism fires automatically, the server is relaunched, and the original request resumes once both servers are back online.

---

### a. Download File

Downloads a file from the distributed file system and saves it to the local `client_files/` directory.

**Flow:**
1. Prompts for a filename (type `!cancel` to abort and return to main menu).
2. Performs a server health check.
3. Sends `OP_RETRIEVE` to Server 1.
4. Server 1 checks its local store and queries Server 2 via `OP_RETRIEVE`.
5. Server 1 applies comparison logic and sends back one of three responses.
6. The client saves the received file(s) to `client_files/`.

**Output scenarios:**

| Server State | Client Output | Saved File(s) |
|---|---|---|
| Found on both, **identical** | `✔ File saved: client_files/received_a.txt (42 bytes)` | `received_<name>` |
| Found on both, **different** | `⚠ File exists on BOTH servers but copies are DIFFERENT:` + two success lines | `received1_<name>` and `received2_<name>` |
| Found on Server 1 only | `✔ File saved: client_files/received_a.txt (42 bytes)` | `received_<name>` |
| Found on Server 2 only | `✔ File saved: client_files/received_a.txt (42 bytes)` | `received_<name>` |
| Not found on either | `✘ File 'a.txt' was NOT FOUND on any server.` | (nothing saved) |

**Example runtime session:**
```
──────────────────────────────────────────────────────
  Download File
──────────────────────────────────────────────────────
  Type !cancel at any prompt to return to main menu.

  ▶  Enter file name: a.txt
  ✔  File saved: client_files/received_a.txt  (13 bytes)
```

---

### b. File Size

Reports the size of a file on each server without downloading its contents.

**Flow:**
1. Prompts for a filename.
2. Performs a server health check.
3. Sends `OP_SIZE` to Server 1.
4. Server 1 checks its local store for the file size and queries Server 2 for its size via `OP_SIZE`.
5. Server 1 sends back four values: `s1_found`, `s1_size`, `s2_found`, `s2_size`.
6. The client displays a human-readable summary.

**Output scenarios:**

| Server State | Display |
|---|---|
| Found on both, **same size** | `Found on both Server 1 and Server 2. Both copies are identical in size → 42 bytes` |
| Found on both, **different sizes** | `Found on both Server 1 and Server 2, sizes differ:` + `Server 1: 42 bytes` + `Server 2: 38 bytes` |
| Found on **Server 1 only** | `Found on Server 1 only. Size: 42 bytes` |
| Found on **Server 2 only** | `Found on Server 2 only. Size: 38 bytes` |
| Not found on either | `✘ File 'x.txt' was NOT FOUND on any server.` |

**Example runtime session:**
```
──────────────────────────────────────────────────────
  File Size
──────────────────────────────────────────────────────
  Type !cancel at any prompt to return to main menu.

  ▶  Enter file name: b.txt
  ══════════════════════════════════════════════════════
  Found on both Server 1 and Server 2, sizes differ:
    Server 1: 16 bytes
    Server 2: 16 bytes
  ══════════════════════════════════════════════════════
```

---

### c. Create File

Creates a new file on one or both servers, optionally with content.

**Sub-menu:**
```
  Where would you like to create the file?

    a. Both servers
    b. Server 1 only
    c. Server 2 only
    r. Return to main menu
    e. Exit
```

**Flow:**
1. Choose a target (both / S1 only / S2 only).
2. Prompted for a filename with extension (e.g. `notes.txt`). Type `!cancel` to abort.
3. Prompted: `Add content to the file? [y/n]:`
   - **`n`** — an empty file is created.
   - **`y`** — a multi-line content editor opens:
     ```
       Enter file content below. Type a single '.' on its own line to finish.
       (Type !cancel to abort)

       Hello, this is line one.
       This is line two.
       .
     ```
     Type each line and press Enter. Type a lone `.` on its own line to finish input.
4. Performs a server health check.
5. Sends `OP_CREATE` with the target, filename, and content length+bytes to Server 1.
6. Server 1 creates the file locally (if target includes S1) and forwards the creation request to Server 2 (if target includes S2).
7. Server 1 responds with per-server status codes.
8. The client displays a per-server success/failure/skipped summary.

**Output scenarios:**

| Target | S1 Result | S2 Result |
|---|---|---|
| Both | `✔ Server 1 create: Success` | `✔ Server 2 create: Success` |
| Server 1 only | `✔ Server 1 create: Success` | `ℹ Server 2 create: (skipped)` |
| Server 2 only | `ℹ Server 1 create: (skipped)` | `✔ Server 2 create: Success` |

If creation fails on a server (e.g. permission error), `✘ Server N create: FAILED` is shown.

**Example runtime session:**
```
──────────────────────────────────────────────────────
  Create File
──────────────────────────────────────────────────────
  Where would you like to create the file?

    a. Both servers
    b. Server 1 only
    c. Server 2 only
    r. Return to main menu
    e. Exit

Your choice: a

  ▶  Enter file name (with extension): hello.txt

Add content to the file? [y/n]: y
  Enter file content below. Type a single '.' on its own line to finish.
  (Type !cancel to abort)

Hello world!
This is a test.
.

  ✔  Server 1 create: Success
  ✔  Server 2 create: Success
```

---

### d. List Files

Lists all files stored on one or both servers, including their sizes. Files that exist on both servers with the same size are annotated with `(identical)`.

**Sub-menu:**
```
  Which server files would you like to list?

    a. Both servers
    b. Server 1 only
    c. Server 2 only
    r. Return to main menu
    e. Exit
```

**Flow:**
1. Choose a scope (all / S1 / S2).
2. Performs a server health check.
3. Sends `OP_LIST` with the target to Server 1.
4. Server 1 reads its local directory (if target includes S1) and queries Server 2 for its directory listing (if target includes S2), then sends both lists back.
5. The client displays files grouped by server, with `(identical)` annotation for files present on both servers with matching sizes.

**Output example (both servers):**
```
──────────────────────────────────────────────────────
  File Listing
──────────────────────────────────────────────────────

  SERVER 1 files (3):
    a.txt          13 bytes  (identical)
    b.txt          16 bytes
    test.txt        9 bytes

  SERVER 2 files (2):
    a.txt          13 bytes  (identical)
    b.txt          16 bytes

──────────────────────────────────────────────────────
```

Note: `(identical)` means the file name and file size match across both servers. It is possible for two files of the same size to have different content — the `view` command reveals actual content differences.

---

### e. View File

Displays the content of a file directly in the terminal, along with metadata (location, size, identical/different status). This is the "read without downloading" option.

**Flow:**
1. Prompts for a filename. Type `!cancel` to abort.
2. Performs a server health check.
3. Sends `OP_SIZE` to Server 1 to gather metadata (size per server, found/not found per server).
4. Sends `OP_RETRIEVE` to Server 1 to fetch the actual content.
5. The client displays metadata first, then content with line numbers.

**Output scenarios:**

**Single copy (identical on both, or only on one server):**
```
  ══════════════════════════════════════════════════════
  File:  a.txt
  Location:  Server 1  |  Server 2
  Size:  13 bytes (identical on both)
  ══════════════════════════════════════════════════════
     1  Hello, world!
  ══════════════════════════════════════════════════════
```

**Divergent copies (different content on each server):**
```
  ══════════════════════════════════════════════════════
  File:  b.txt
  Server 1 Contents (16 bytes):
  ══════════════════════════════════════════════════════
     1  server1 version
  ══════════════════════════════════════════════════════

  Server 2 Contents (16 bytes):
  ══════════════════════════════════════════════════════
     1  server2 version
  ══════════════════════════════════════════════════════
```

**Not found:**
```
  ✘  File 'ghost.txt' was NOT FOUND on any server.
```

---

### f. Delete File

Deletes a file from one or both servers. The menu options available are dynamically generated based on where the file was actually found.

**Flow:**
1. Prompts for a filename. Type `!cancel` to abort.
2. Performs a server health check.
3. Sends `OP_DELETE` — Phase 1 — to Server 1. Server 1 checks its local store and asks Server 2 whether it has the file. Server 1 sends back `s1_found` and `s2_found`.
4. The client shows where the file was found and presents a **dynamic menu**:
   - If found on **both**: options are `a. Both servers`, `b. Server 1 only`, `c. Server 2 only`, `r. Return`.
   - If found on **Server 1 only**: options are `a. Server 1`, `r. Return`.
   - If found on **Server 2 only**: options are `a. Server 2`, `r. Return`.
   - If found on **neither**: an error is displayed and the prompt returns.
5. The user picks a deletion target.
6. The client sends the target byte back — Phase 2 — over the same open connection to Server 1.
7. Server 1 deletes locally (if applicable) and forwards the delete command to Server 2 (if applicable).
8. Server 1 sends back per-server deletion results.
9. The client displays a per-server success/skip/error summary.

**Example runtime session (file on both servers):**
```
──────────────────────────────────────────────────────
  Delete File
──────────────────────────────────────────────────────
  Type !cancel at any prompt to return to main menu.

  ▶  Enter file name: b.txt

  File 'b.txt' found on:
  ✔  Server 1
  ✔  Server 2

  Delete from:
    a. Both servers
    b. Server 1 only
    c. Server 2 only
    r. Return to main menu

Your choice: a

  ✔  Server 1: Deleted
  ✔  Server 2: Deleted
```

**Example runtime session (file only on Server 1):**
```
  File 'test.txt' found on:
  ✔  Server 1

  Delete from:
    a. Server 1
    r. Return to main menu

Your choice: a

  ✔  Server 1: Deleted
  ℹ  Server 2: Skipped
```

---

### g. Exit

Gracefully shuts down both servers and exits the client.

**Flow:**
1. Sends `OP_SHUTDOWN` to Server 1.
2. Server 1 forwards the shutdown signal to Server 2 and acknowledges.
3. The client polls Server 1 (via repeated `OP_PING` attempts) until it stops responding — confirming it has shut down.
4. The client displays a shutdown confirmation and exits.

```
──────────────────────────────────────────────────────
  Shutting Down
──────────────────────────────────────────────────────
  Sending shutdown signal to servers...

  ℹ  Waiting for servers to go offline...
  ✔  Shutdown successful. Both servers are offline.

  Goodbye!
```

This `Exit` behaviour is consistent across all sub-menus — whenever `e. Exit` is selected in any sub-menu (Create, List, Delete scopes), the same graceful shutdown and exit sequence runs.

---

### The `!cancel` Escape Token

At any prompt where you are asked to type a filename or file content, typing `!cancel` (case-sensitive) and pressing Enter will **immediately abort the current operation and return you to the main menu** without sending any request to the servers.

This is intentional: pressing `Ctrl+C` in a terminal would kill the entire client process, losing your session. The `!cancel` token lets you bail out of any individual operation cleanly while keeping the client running.

**Where it works:**
- "Enter file name:" prompt in Download, Size, View, Delete, and Create.
- "Enter file name (with extension):" prompt in Create.
- Any line during multi-line content entry in Create (including mid-way through typing content).

**Where it does not apply:**
- Single-letter menu choices (`a`, `b`, `c`, etc.) — those are single keystrokes.
- The `y/n` startup question.

---

## Server Auto-Recovery — All Scenarios

The auto-recovery system ensures that if any server goes offline — either before or during a command — the client:
1. Detects the outage.
2. Auto-launches the affected server(s) in a new terminal window of the same terminal application.
3. Waits (with a spinner) for the server(s) to come back online.
4. Resumes the original request **without requiring the user to re-enter anything**.

Recovery is triggered at two points for every command:
- **Before** the server request (pre-flight check via `OP_PING`).
- **During** a request if the TCP connection drops unexpectedly mid-receive.

### How Recovery is Triggered

Every command calls `ensure_servers_up(context)` before making its server request, where `context` is a human-readable label of the pending operation (e.g. `"download 'a.txt'"`). This function:

1. Calls `check_server_status()` — sends `OP_PING` to Server 1, which in turn pings Server 2 and reports both statuses.
2. If both are online → returns immediately (fast path, no visible output).
3. If one or both are offline → enters recovery mode and prints status.

After recovery succeeds, the function prints:
```
  All servers online. Resuming: download 'a.txt'
```
And control returns to the caller, which retries the original request using the same locally-stored state (filename, content, target — all preserved in stack variables).

---

### Scenario 1 — Server 2 Goes Offline

**Situation:** Server 2 process dies while Server 1 is still running.

**Detection:** `OP_PING` reaches Server 1 successfully. Server 1 tries to ping Server 2 and gets no response. Server 1 reports `s1_up=1, s2_up=0`.

**Recovery flow:**
```
  ⚠  Server disruption detected while processing your request.

  Server 1 [:5001]  ● ONLINE
  Server 2 [:5002]  ○ OFFLINE

  ⚠  Server 2 is offline — auto-recovering...
  ℹ  Launching Server 2 terminal...
     Launched Server 2 terminal.

  ℹ  Waiting for Server 2 to come online...
     Waiting for servers... (attempt 1/100)  [S2 waiting]
     ...
  ✔  Server 2 — back ONLINE

  All servers online. Resuming: download 'a.txt'
```

Only one new terminal is launched (for Server 2). Server 1 is untouched.

---

### Scenario 2 — Server 1 Goes Offline (Server 2 Still Running)

**Situation:** Server 1 process dies while Server 2 is still running.

**Detection:** `check_server_status()` tries `connect_to_server1()` — connection refused. `s1=0`. Since the client has no channel to ask Server 2 directly (architecture rule: client never contacts S2), `s2` is initially unknown.

**Recovery flow — two phases:**

**Phase 1 — Recover Server 1:**
```
  ⚠  Server disruption detected while processing your request.

  Server 1 [:5001]  ○ OFFLINE
  Server 2 [:5002]  ○ OFFLINE (unknown — cannot confirm without Server 1)

  ⚠  Server 1 is offline — recovering Server 1 first...
  ℹ  Launching Server 1 terminal...
     Launched Server 1 terminal.

  ℹ  Waiting for Server 1 to come online...
     Waiting for servers... (attempt 1/100)  [S1 waiting]
     ...
  ✔  Server 1 — back ONLINE
```

**Phase 2 — Check Server 2 via Server 1 (correct architecture):**

Once Server 1 is confirmed up, `OP_PING` is sent to Server 1 again. Server 1 now pings Server 2 and reports its real status.

**Sub-case A — Server 2 is still running:**
```
  ✔  Server 2 — already ONLINE (confirmed via Server 1)

  All servers online. Resuming: download 'a.txt'
```
No second terminal is launched. Only one new terminal was opened in total.

**Sub-case B — Server 2 also went offline:**
```
  ⚠  Server 1 reports Server 2 is also offline — recovering Server 2...

  Server 1 [:5001]  ● ONLINE
  Server 2 [:5002]  ○ OFFLINE

  ℹ  Launching Server 2 terminal...
     Launched Server 2 terminal.

  ℹ  Waiting for Server 2 to come online...
     Waiting for servers... (attempt 1/100)  [S2 waiting]
     ...
  ✔  Server 2 — back ONLINE

  All servers online. Resuming: download 'a.txt'
```

---

### Scenario 3 — Both Servers Go Offline

**Situation:** Both server processes die simultaneously (e.g. machine restart, explicit kill).

**Detection:** `connect_to_server1()` fails → `s1=0`. Since S1 is unreachable we cannot ask it about S2, so S2 status is also `0` (unknown but presumed down).

This is treated as a special sub-case of Scenario 2 (Server 1 is down). The same two-phase recovery runs:
1. Launch and wait for Server 1.
2. Once Server 1 is up, ask it about Server 2. If Server 2 is also down, launch and wait for Server 2 too.

The end result is always: both servers are confirmed online before resuming.

---

### Recovery During Mid-Request Disconnect

If the TCP connection to Server 1 drops **after** the request has already been sent (e.g. Server 1 crashes while streaming a large file back), the client catches the failed `recv()` and immediately triggers recovery:

```
  ⚠  Lost connection to server mid-request.
  [recovery flow as above]
  All servers online. Resuming: download 'a.txt'
```

The entire request is retried from the beginning using the already-captured filename/content/target stored in local variables. The user never has to type anything again.

---

### Recovery Timeout

The recovery poller runs for a maximum of **100 attempts**, checking every **3 seconds** — a total wait window of up to **5 minutes**. If the server does not come online within that window:

```
  ✘  Server 1 could not be recovered within the timeout.
  ℹ  Please start Server 1 manually:
      ./bin/server1 5001 127.0.0.1 5002 ./server1_files
```

The operation is aborted and the main menu is shown. No data is lost — the user can retry the command manually once they've addressed the server issue.

---

## File Distribution Use Cases

These are the five fundamental scenarios the system is designed to handle, applicable to the Download, View, and Size commands:

### Case 1 — File identical on both servers
- Both Server 1 and Server 2 have the file with byte-for-byte identical content.
- Server 1 compares both copies using `memcmp()`.
- **Download result:** Client receives **one file**, saved as `client_files/received_<name>`.
- **View result:** Content displayed once with `Location: Server 1 | Server 2`.
- **Size result:** `Found on both Server 1 and Server 2. Both copies are identical in size → N bytes`.

### Case 2 — File different on both servers (replication divergence)
- Both servers have the file but with different content (simulating replication lag).
- Server 1 detects the mismatch via `memcmp()` and sends both copies.
- **Download result:** Client receives **two files** — `client_files/received1_<name>` (Server 1's copy) and `client_files/received2_<name>` (Server 2's copy).
- **View result:** Server 1 and Server 2 content blocks shown side by side with line numbers.
- **Size result:** `Found on both Server 1 and Server 2, sizes differ: Server 1: N1 bytes / Server 2: N2 bytes`.

### Case 3 — File only on Server 1
- Server 1 has the file; Server 2 reports not found.
- **Download result:** Client receives **one file**, saved as `client_files/received_<name>`.
- **Size result:** `Found on Server 1 only. Size: N bytes`.

### Case 4 — File only on Server 2
- Server 2 has the file; Server 1 does not.
- Server 1 proxies Server 2's copy to the client.
- **Download result:** Client receives **one file**, saved as `client_files/received_<name>`.
- **Size result:** `Found on Server 2 only. Size: N bytes`.

### Case 5 — File not found on any server
- Neither server has the file.
- **Download result:** `✘ File 'x.txt' was NOT FOUND on any server.` Nothing saved.
- **Size result:** `✘ File 'x.txt' was NOT FOUND on any server.`

---

## Test Data Setup

The project ships with pre-populated test directories to exercise all five use cases:

```
server1_files/
├── a.txt     → "hello world"     (same as server2/a.txt  → Case 1: identical)
├── b.txt     → "server1 version" (differs from server2   → Case 2: different)
└── test.txt  → "test content"    (absent from server2    → Case 3: S1 only)

server2_files/
├── a.txt     → "hello world"     (same as server1/a.txt  → Case 1: identical)
├── b.txt     → "server2 version" (differs from server1   → Case 2: different)
└── (any file absent from server1_files              →  Case 4: S2 only)
```

A filename that exists in neither directory covers Case 5.

---

## Unit Testing

The project ships with a Unity-based unit test suite that covers all pure-logic functions and guards across all three source files. Unity is a lightweight, single-file C testing framework with no external dependencies.

### Running the Tests

```bash
make test
```

This compiles each test suite into its own binary in `bin/`, runs them all sequentially, and prints a grand total at the end:

```
──────────────────────────────────────────
  Running: test_pathsanitize
──────────────────────────────────────────
test/test_pathsanitize.c:64:test_traversal_blocked:PASS
...
══════════════════════════════════════════
  GRAND TOTAL
══════════════════════════════════════════
  60 Tests  0 Failures  0 Ignored
  ALL TESTS PASSED
══════════════════════════════════════════
```

### Test Suites

| File | Function Under Test | Tests |
|---|---|---|
| `test_pathsanitize.c` | `build_fullpath()` — path traversal and safety | 4 |
| `test_sanitize_name.c` | `sanitize_name()` — client filename sanitisation | 3 |
| `test_cancel_token.c` | `is_cancel()` — escape token detection | 4 |
| `test_buffers_equal.c` | `buffers_equal()` — byte-level file content comparison | 4 |
| `test_server_status_code.c` | `server_status_code()` — S1/S2 boolean → status code mapping | 4 |
| `test_file_identical.c` | `file_is_identical_in_list()` — cross-server identical file detection | 6 |
| `test_server_ready.c` | `!need \|\| up` — server polling readiness guard | 4 |
| `test_recv_length_guard.c` | `recv_string` length validation — wire protocol safety | 6 |
| `test_file_size_guard.c` | `read_file_to_buf` size guard — `off_t` → `uint32_t` overflow check | 5 |
| `test_target_label.c` | Target enum → label ternary chains in client + server1 | 10 |

All test files are self-contained — each file copies the function under test directly rather than including a whole source file, avoiding `main()` conflicts and keeping compilation times negligible.

Runtime integration testing (all menu paths, all file distribution scenarios, all three recovery scenarios, and all edge cases) has been completed manually on macOS (iTerm2).

---

## Code Walkthrough

### Shared Protocol Primitives

All three source files share two low-level socket helpers that guarantee complete transmission over TCP (which may deliver data in fragments):

#### `recv_all(sock, buf, n)`
Loops calling `recv()` until exactly `n` bytes have been read into `buf`. Returns `-1` on connection close or error, `0` on success. Handles `EINTR` (signal interruptions) transparently.

#### `send_all(sock, buf, n)`
Loops calling `send()` until exactly `n` bytes from `buf` have been written to the socket. Returns `-1` on error, `0` on success. Handles `EINTR` transparently.

#### `recv_u32(sock, out)` / `send_u32(sock, val)`
Read/write a single big-endian `uint32_t` over the socket, converting to/from host byte order via `ntohl` / `htonl`.

#### `send_string(sock, str)` / `recv_string` equivalent
Sends a 4-byte length prefix followed by the string bytes. Used for all filename transmissions.

#### `build_fullpath(base_dir, name, out, cap)` (servers only)
Constructs an absolute path by joining `base_dir` and `name`. Rejects any name containing `..` (directory traversal) or starting with `/` (absolute path injection). This prevents path-traversal security vulnerabilities.

---

### `server2.c` — Replica File Server

**Role:** Passive replica. Accepts connections **exclusively from Server 1** and responds to opcoded requests.

**Startup:** `./bin/server2 <port> <base_dir>`

**Signal handling:** `signal(SIGPIPE, SIG_IGN)` — prevents the server from crashing if Server 1 disconnects unexpectedly mid-send.

**Main dispatch loop (per connection from Server 1):**

Reads a single opcode byte and dispatches to the appropriate handler:

| Opcode | Handler | Behaviour |
|--------|---------|-----------|
| `OP_PING` (0x01) | `handle_ping` | Sends `[uint32: 1]` — confirms Server 2 is online |
| `OP_RETRIEVE` (0x02) | `handle_retrieve` | Reads filename, finds file, sends `[status][len?][bytes?]` |
| `OP_SIZE` (0x03) | `handle_size` | Reads filename, stat()s file, sends `[found][size]` |
| `OP_CREATE` (0x04) | `handle_create` | Reads filename+content, creates file, sends `[status]` |
| `OP_LIST` (0x05) | `handle_list` | Reads base_dir, sends `[count]` + per-file `[namelen][name][size]` |
| `OP_DELETE` (0x06) | `handle_delete` | Reads filename, deletes if present, sends `[status]` |
| `OP_SHUTDOWN` (0x07) | `handle_shutdown` | Sends ack, calls `exit(0)` |

All log lines explicitly state the request came from Server 1, e.g.:
```
[SERVER2] DOWNLOAD request received from SERVER1 — file: 'a.txt'
[SERVER2] File found — sending 13 bytes to SERVER1
```

---

### `server1.c` — Primary File Server / Orchestrator

**Role:** Accepts all client requests, performs local file operations, queries Server 2 as needed, applies decision logic, and responds to the client.

**Startup:** `./bin/server1 <listen_port> <server2_ip> <server2_port> <base_dir>`

**Signal handling:** `signal(SIGPIPE, SIG_IGN)` — prevents crashes if the client disconnects mid-send.

**Helper `s2_open(opcode)`:** Opens a fresh TCP connection to Server 2 and sends the given opcode byte. Returns the socket fd on success, `-1` if Server 2 is unreachable. Used by every handler that needs to delegate to Server 2.

**Main dispatch loop (per client connection):**

| Opcode | Handler | Behaviour |
|--------|---------|-----------|
| `OP_PING` | `handle_ping` | Pings Server 2, returns `[s1_up=1][s2_up]` to client |
| `OP_RETRIEVE` | `handle_retrieve` | Gets file from local + S2, compares, sends 0/1/2 response |
| `OP_SIZE` | `handle_size` | Stat()s locally + queries S2, sends four-field size response |
| `OP_CREATE` | `handle_create` | Creates locally (if target includes S1), forwards to S2 (if target includes S2), sends combined status |
| `OP_LIST` | `handle_list` | Reads local directory (if target includes S1), forwards to S2 (if target includes S2), sends combined listing |
| `OP_DELETE` | `handle_delete` | Phase 1: checks existence locally + S2, sends found flags. Phase 2: reads del_target, deletes locally/forwards, sends combined result |
| `OP_SHUTDOWN` | `handle_shutdown` | Forwards shutdown to Server 2, acks client, calls `exit(0)` |

**RETRIEVE decision logic (core of the assignment):**

| `found1` | `found2` | Content match? | Action |
|----------|----------|----------------|--------|
| No | No | — | Send `status=0` (NOT FOUND) |
| Yes | No | — | Send `status=1` + Server 1's content |
| No | Yes | — | Send `status=1` + Server 2's content |
| Yes | Yes | Identical | Send `status=1` + Server 1's content (single agreed copy) |
| Yes | Yes | Different | Send `status=2` + Server 1's content + Server 2's content |

All log lines explicitly narrate the orchestration flow, e.g.:
```
[SERVER1] DOWNLOAD request received from client — file: 'b.txt'
[SERVER1] Local file found (16 bytes)
[SERVER1] Forwarding DOWNLOAD request to SERVER2 for 'b.txt'...
[SERVER1] SERVER2 has the file (16 bytes)
[SERVER1] Both copies exist but are DIFFERENT — sending both to client
[SERVER1] Sending dual-file response to client
```

---

### `client.c` — Interactive Terminal Application

**Role:** The user-facing interactive application. Manages the entire lifecycle of the distributed file system session.

**Startup:** `./bin/client [server1_ip] [server1_port]`
Defaults: `127.0.0.1 5001`.

**Key implementation components:**

#### Terminal Detection (`detect_terminal` / `TermType`)
At startup, the client inspects environment variables (`TERM_PROGRAM`, `TERM`, `WSL_DISTRO_NAME`, `WT_SESSION`) and checks for installed terminal binaries to identify the current terminal. The detected type is stored in the global `g_ttype` and used whenever a new server terminal window needs to be launched.

Supported terminals: iTerm2, Apple Terminal, GNOME Terminal, Konsole, XFCE Terminal, xterm, Windows CMD, PowerShell, WSL, and headless Linux (no display — servers run as `nohup` background processes).

#### Launch Script Generation (`write_launch_script`)
On macOS/Linux, instead of embedding shell commands directly in `osascript` or terminal command strings (which breaks when the path contains spaces or special characters), the client writes a small temporary executable shell script to `/tmp/dfs_launch_<PID>_s<N>.sh`. The terminal emulator then simply executes this script by its clean path.

Scripts are given unique names per server (`_s1`, `_s2`) so that two concurrent launches cannot overwrite each other, which was an earlier bug that caused both terminals to start the same server.

#### Server Health Check (`check_server_status` / `server_status_code`)
Sends `OP_PING` to Server 1. Server 1 replies with `[uint32: 1][uint32: s2_status]` — the client's only legitimate channel to learn Server 2's status. When Server 1 is unreachable, `s2` is set to `0` (unknown) and the recovery logic handles the phased recovery.

The raw boolean pair is converted to a single integer status code by `server_status_code(s1, s2)`: `3` = both online, `1` = S1 online only, `0` = S1 offline. This function is extracted as a named helper so it can be independently unit-tested.

#### Auto-Recovery (`ensure_servers_up` / `recover_one_server`)
`ensure_servers_up(context)` is called before every server request in every command function. It uses `recover_one_server(server_num, need_s1, need_s2)` as a shared helper to avoid code duplication across the three recovery branches (S1 down, S2 down, both down). See the [Server Auto-Recovery](#server-auto-recovery--all-scenarios) section for full scenario details.

#### Poll Loop (`wait_for_servers`)
Polls `check_server_status()` every `POLL_INTERVAL` seconds (3 s) for up to `POLL_MAX` attempts (100) — a 5-minute window — displaying a spinner animation. The `need_s1` / `need_s2` flags control which servers are waited on, so phase-1 (wait for S1 only) and phase-2 (wait for S2 only) recovery can reuse the same function.

#### Input Handling (`read_line`, `read_choice`, `is_cancel`)
- `read_line` reads a full line of input, stripping the trailing newline.
- `read_choice` reads a single character and discards the rest of the line.
- `is_cancel(str)` returns true if `str` equals the `CANCEL_TOKEN` (`"!cancel"`).

#### Identical File Detection (`file_is_identical_in_list`)
Used by the `d. List Files` display layer to annotate files that exist on both servers with the same name and size as `(identical)`. Extracted as a named helper — `file_is_identical_in_list(src, idx, other, count)` — so it can be unit-tested independently of the display code.

#### Path Sanitisation (`sanitize_name`)
Before writing received files to disk, the output filename is sanitised: `/`, `\`, and `..` sequences are replaced with underscores. This prevents a malicious server from causing the client to write outside `client_files/`.

#### `ensure_client_files_dir()`
Creates the `client_files/` directory (mode `0755`) if it does not already exist, before any download or view operation writes to it.

---

## Platform Support

| Platform | Status | Server launching |
|---|---|---|
| macOS (iTerm2) | ✅ Tested | `osascript` + temp script |
| macOS (Apple Terminal) | ✅ Supported | `osascript` + temp script |
| Linux (GNOME Terminal) | ✅ Supported | `gnome-terminal --` |
| Linux (Konsole) | ✅ Supported | `konsole -e` |
| Linux (XFCE Terminal) | ✅ Supported | `xfce4-terminal -e` |
| Linux (xterm fallback) | ✅ Supported | `xterm -e` |
| **Linux headless / SSH / cloud VM** | ✅ Supported | `nohup` background process |
| Windows (CMD) | ✅ Supported | `start cmd.exe /k` |
| Windows (PowerShell) | ✅ Supported | `start powershell.exe -NoExit` |
| WSL | ✅ Supported | `start wsl.exe -e bash` |

### Headless Linux (SSH / Cloud VM)

When the client detects that neither `DISPLAY` nor `WAYLAND_DISPLAY` is set (i.e. no graphical session is available — typical of an SSH connection into a cloud instance such as AWS EC2, GCP, or any headless server), it automatically switches to **headless mode**:

- Server processes are launched directly as background processes using `nohup`.
- stdout and stderr for each server are redirected to `/tmp/dfs_server_s1.log` and `/tmp/dfs_server_s2.log`.
- No terminal window is opened; the client polls for the servers to become available exactly as it does in GUI mode.

To inspect server output in headless mode:
```bash
tail -f /tmp/dfs_server_s1.log
tail -f /tmp/dfs_server_s2.log
```

On **Windows**, Winsock 2 (`ws2_32.lib`) is used instead of POSIX sockets. ANSI colour codes are enabled via `SetConsoleMode` with `ENABLE_VIRTUAL_TERMINAL_PROCESSING`. The Makefile detects Windows automatically and appends `.exe` to binary names and links `ws2_32`.

On **macOS/Linux**, `signal(SIGPIPE, SIG_IGN)` is set in both servers so that a client disconnect mid-send does not terminate the server process.

---

## Runtime Terminal Walkthrough

Below is a complete walkthrough of a typical session, from fresh start to exit.

### Step 1 — Launch the client

```bash
cd /path/to/bits-wilp-cc-sem2-dist-comp-assign1
make run
```

### Step 2 — Startup prompt

```
══════════════════════════════════════════════════════
  Distributed File System Client
  Server 1: 127.0.0.1:5001   Server 2: localhost:5002
══════════════════════════════════════════════════════

Would you like to start the app? [y/n]: y

  ℹ  Checking server availability...

  Server 1 [:5001]  ○ OFFLINE
  Server 2 [:5002]  ○ OFFLINE

  ℹ  Starting Server 2...
     Launched Server 2 terminal.
  ℹ  Starting Server 1...
     Launched Server 1 terminal.

  ℹ  Starting up the app. Please wait...
  ⠋  Waiting for servers... (attempt 1/100)  [S1 waiting] [S2 waiting]
  ⠙  Waiting for servers... (attempt 2/100)  [S1 waiting] [S2 waiting]
  ⠹  Waiting for servers... (attempt 3/100)  [S1 waiting]

  ✔  Server 1 — ONLINE
  ✔  Server 2 — ONLINE

  App successfully started!
```

### Step 3 — Use the main menu

```
══════════════════════════════════════════════════════
  Distributed File System — Main Menu
══════════════════════════════════════════════════════
    a.  Download file
    b.  File size
    c.  Create file
    d.  List files
    e.  View file
    f.  Delete file
    g.  Exit

Your choice: a
```

### Step 4 — Download a file

```
──────────────────────────────────────────────────────
  Download File
──────────────────────────────────────────────────────
  Type !cancel at any prompt to return to main menu.

  ▶  Enter file name: a.txt
  ✔  File saved: client_files/received_a.txt  (13 bytes)
```

### Step 5 — List all files

```
Your choice: d

──────────────────────────────────────────────────────
  List Files
──────────────────────────────────────────────────────
  Which server files would you like to list?

    a. Both servers
    b. Server 1 only
    c. Server 2 only
    r. Return to main menu
    e. Exit

Your choice: a

──────────────────────────────────────────────────────
  File Listing
──────────────────────────────────────────────────────

  SERVER 1 files (3):
    a.txt          13 bytes  (identical)
    b.txt          16 bytes
    test.txt        9 bytes

  SERVER 2 files (2):
    a.txt          13 bytes  (identical)
    b.txt          16 bytes

──────────────────────────────────────────────────────
```

### Step 6 — Mid-session server outage (auto-recovery)

Suppose Server 2 crashes while you are in the middle of a download:

```
Your choice: a

──────────────────────────────────────────────────────
  Download File
──────────────────────────────────────────────────────
  Type !cancel at any prompt to return to main menu.

  ▶  Enter file name: b.txt

  ⚠  Server disruption detected while processing your request.

  Server 1 [:5001]  ● ONLINE
  Server 2 [:5002]  ○ OFFLINE

  ⚠  Server 2 is offline — auto-recovering...
  ℹ  Launching Server 2 terminal...
     Launched Server 2 terminal.

  ℹ  Waiting for Server 2 to come online...
  ⠋  Waiting for servers... (attempt 1/100)  [S2 waiting]
  ⠙  Waiting for servers... (attempt 2/100)  [S2 waiting]

  ✔  Server 2 — back ONLINE

  All servers online. Resuming: download 'b.txt'

  ⚠  File exists on BOTH servers but copies are DIFFERENT:
  ✔  Server 1 copy saved: client_files/received1_b.txt  (16 bytes)
  ✔  Server 2 copy saved: client_files/received2_b.txt  (16 bytes)
```

The user never had to re-type the filename or re-select the menu option.

### Step 7 — Exit

```
Your choice: g

──────────────────────────────────────────────────────
  Shutting Down
──────────────────────────────────────────────────────
  Sending shutdown signal to servers...

  ℹ  Waiting for servers to go offline...
  ✔  Shutdown successful. Both servers are offline.

  Goodbye!
```

Both server terminal windows will display their respective shutdown log lines and then return to a bash prompt.
