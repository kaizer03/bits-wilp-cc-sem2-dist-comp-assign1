# Contributions — Distributed File System

**BITS Pilani WILP | Cloud Computing | CISS Assignment 1**

---

## Team Members & Contributions

| Sl. No. | Team Member | ID No. | Contributions |
|---|---|---|---|
| 1 | **Moulik Patra** | 2025MT03009 | **Client Implementation & System Architecture.** Contributed to the overall system architecture following the strict `Client → Server1 → Server2` communication hierarchy. Worked on the core original-problem-statement client behaviour: establishing the TCP connection to Server 1, sending the file request (pathname), receiving the status response, branching on `status=0` (not found), `status=1` (single copy), and `status=2` (two divergent copies), and writing received files to disk. Implemented the original client file-save logic including safe output filename construction. Extended the client into a fully interactive terminal application — startup flow, all seven main menu commands (`download`, `size`, `create`, `list`, `view`, `delete`, `exit`), sub-menus, the `!cancel` escape token mechanism, multi-line content input for file creation, `client_files/` output directory management, ANSI colour output with automatic TTY detection, and the `read_line` / `read_choice` / `is_cancel` input handling primitives. Authored the `Makefile`, `README.md`, and `CONTRIBUTIONS.md`. Managed the Git repository and raised the feature PR. |
| 2 | **Yuvraj Vijay Vedapathak** | 2025MT03141 | **Server 1 — Primary Server, Orchestrator & Core Decision Logic.** Implemented the original-problem-statement behaviour in `server1.c`: accepting client connections, receiving the file pathname, reading the local file from `server1_files/`, forwarding the same request to Server 2, receiving Server 2's response, and applying the five-case decision logic — (1) not found on either, (2) found on S1 only, (3) found on S2 only, (4) found on both with identical content, (5) found on both with divergent content — using `buffers_equal()` (`memcmp`-based byte-perfect comparison) to distinguish cases 4 and 5. Implemented `read_file_to_buf` for heap-buffered file reads using `fstat`. Implemented `s2_open(opcode)` for opening fresh Server 2 connections. Extended `server1.c` with the full opcode-dispatch loop and six additional handlers: `handle_ping`, `handle_size`, `handle_create`, `handle_list`, `handle_delete`, `handle_shutdown`. Added `signal(SIGPIPE, SIG_IGN)`. Authored all `[SERVER1]` runtime log messages. |
| 3 | **Ravi Kumar Atrey** | 2025MT03129 | **Server 2 — Replica File Server & Path Security.** Implemented the original-problem-statement replica behaviour in `server2.c`: accepting connections from Server 1, receiving the requested pathname, looking up the file in `server2_files/`, and responding with either `status=0` (not found) or `status=1` + file length + file bytes. Implemented `build_fullpath` — the path-sanitisation function shared across both servers — which blocks directory traversal (`..`) and absolute path injection (`/`) to prevent security vulnerabilities. Implemented `read_file_to_buf` on the Server 2 side for heap-buffered full-file reads. Designed and maintained the `server2_files/` test data directory covering all five use cases (identical file, divergent file, S2-only file). Extended `server2.c` with the full opcode-dispatch loop and six additional handlers: `handle_ping`, `handle_size`, `handle_create`, `handle_list`, `handle_delete`, `handle_shutdown`. Added `signal(SIGPIPE, SIG_IGN)`. Authored all `[SERVER2]` log messages explicitly annotating that every request originates from Server 1. |
| 4 | **Soumyajyoti Biswas** | 2025MT03071 | **Binary Wire Protocol Design & Auto-Recovery Engine.** Designed and implemented the shared binary communication protocol that underpins the entire original problem statement — the length-prefixed, network-byte-order message format used by all three components. Implemented the foundational socket primitives `recv_all` and `send_all` (loop-until-complete helpers handling partial TCP delivery and `EINTR`) and the higher-level `recv_u32` / `send_u32` / `send_string` helpers. Defined the five response status codes (`0`=not found, `1`=single copy, `2`=divergent copies) and the wire layout for each. Designed and implemented the complete server auto-recovery subsystem: `ensure_servers_up(context)`, `recover_one_server(num, need_s1, need_s2)`, and `check_server_status`. Implemented the two-phase Server 1 recovery protocol (Phase 1: recover S1; Phase 2: ask S1 via `OP_PING` about S2) that preserves the `Client → S1 → S2` rule with no direct client-to-S2 contact. Implemented `wait_for_servers` polling loop with spinner animation. Implemented mid-request TCP disconnect detection and retry logic. |
| 5 | **Ajeet Kumar Yadav** | 2025MT03083 | **System Integration, Test Data & Cross-Platform Support.** Worked on end-to-end integration of the original problem statement: designed and populated the `server1_files/` and `server2_files/` test data directories to exercise all five use cases (identical content, divergent content, S1-only, S2-only, not found on either). Validated the complete original retrieval flow across all five scenarios to confirm correctness of the comparison and response logic. Implemented the extended opcode-based binary protocol additions — the seven opcodes (`OP_PING` through `OP_SHUTDOWN`) and target flags (`TARGET_BOTH / TARGET_S1 / TARGET_S2`) shared across all three components. Implemented the cross-platform terminal detection subsystem (`detect_terminal` / `TermType` enum) and `launch_terminal` covering all nine terminal types. Implemented `write_launch_script` for macOS/Linux temp-script generation with unique per-server names. Implemented `sanitize_name`, `ensure_client_files_dir`, Windows Winsock 2 initialisation, and Windows ANSI VT colour mode setup. Authored the `Makefile` build targets including `make run` and platform detection. |

---

## Languages & Technologies Used

| Category                    | Details                                                                                                                                         |
|-----------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------|
| **Implementation Language** | C (C99 standard), compiled with `gcc -O2 -Wall -Wextra`                                                                                         |
| **Networking**              | POSIX TCP sockets (`socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`) on macOS/Linux; Winsock 2 (`ws2_32`) on Windows             |
| **IPC / Process Launch**    | `system()` with platform-specific terminal commands; temporary executable shell scripts via `fprintf` + `chmod`                                 |
| **File System APIs**        | `open`, `read`, `write`, `fstat`, `stat`, `unlink`, `opendir`, `readdir`, `closedir`, `mkdir` (POSIX); `CreateFileA`, `WriteFile` (Windows)     |
| **Signal Handling**         | `signal(SIGPIPE, SIG_IGN)` on both servers to prevent crashes on peer disconnect                                                                |
| **Build System**            | GNU Make (`Makefile`) with platform auto-detection for Windows/macOS/Linux                                                                      |
| **Terminal UI**             | ANSI escape codes for colour output; spinner animation via `\r` carriage-return overwrite; `isatty` for TTY detection                           |
| **Version Control**         | Git, GitHub, GitHub CLI (`gh`), branch protection rulesets                                                                                      |
| **Platform Support**        | macOS (Apple Silicon & Intel), Linux (any distro with GCC), Windows (native CMD/PowerShell), Windows Subsystem for Linux (WSL)                 |

---

## Additional Features Implemented (Beyond the Problem Statement)

The original problem statement required only a one-shot command-line file retrieval client. The following features were designed and implemented entirely as extensions by the team:

- **Interactive persistent terminal application** — the client is a long-running process with a full menu system rather than a single-use command. Once started, it keeps running until the user explicitly chooses to exit.
- **Automated server lifecycle management** — on startup the client detects whether servers are running and auto-launches the missing server(s) in new terminal windows without any manual intervention from the user.
- **Cross-platform terminal detection and consistent window launching** — the client detects the active terminal emulator at runtime (from environment variables and binary presence checks) and always opens new server windows in the same terminal application, ensuring a consistent visual experience.
- **Temp-script-based terminal launch** — uniquely named executable shell scripts (`/tmp/dfs_launch_<PID>_s<N>.sh`) are generated per server per launch, avoiding race conditions between concurrent launches and resolving `osascript` quoting failures on macOS paths with spaces.
- **Self-healing auto-recovery from mid-session server outages** — if any server goes offline before or during a command, the client automatically detects the failure, relaunches the affected server(s), waits for them to return, and resumes the interrupted request without requiring user re-input.
- **Architecture-correct two-phase Server 1 recovery** — when Server 1 is offline the client recovers it first, then uses Server 1's `OP_PING` response to determine Server 2's real status. This preserves the `Client → S1 → S2` rule at all times. The client never contacts Server 2 directly.
- **Mid-request TCP disconnect recovery** — `recv` failures mid-stream are caught and trigger the recovery engine, followed by a full request retry using preserved local state.
- **Extended opcode-based binary protocol** — the original protocol handled only file retrieval. Seven opcodes were designed and implemented: `PING`, `RETRIEVE`, `SIZE`, `CREATE`, `LIST`, `DELETE`, `SHUTDOWN`.
- **File size query command** — reports per-server file size with identical/divergent annotation without downloading content.
- **File creation command** — creates files on one or both servers from the client, with support for multi-line content input terminated by a lone `.` on its own line.
- **File listing command** — lists all files on one or both servers with sizes; files present on both servers with matching sizes are annotated as `(identical)`.
- **File view command** — displays file content directly in the client terminal with line numbers and metadata (location, size, status) without saving to disk.
- **File delete command** — uses a two-phase protocol: Phase 1 discovers where the file exists; Phase 2 presents a dynamic menu (options tailored to exactly where the file was found) and executes the deletion.
- **Graceful shutdown command** — sends `OP_SHUTDOWN` through Server 1 to both servers, polls until both are confirmed offline, and exits cleanly.
- **`!cancel` escape token** — typing `!cancel` at any filename or content prompt aborts the current operation and returns to the main menu without killing the client process (unlike `Ctrl+C` which would terminate it).
- **Downloaded files organised into `client_files/` directory** — all received files are saved into a dedicated subdirectory rather than the working directory root.
- **ANSI colour terminal output** — all client output uses colour-coded messages (`✔` green for success, `✘` red for errors, `⚠` yellow for warnings, `ℹ` cyan for info) with automatic detection and fallback to plain text when not running in a TTY.
- **Spinner loading animation** — a rotating character spinner (`⠋ ⠙ ⠹ ⠸ ⠼ ⠴ ⠦ ⠧ ⠇ ⠏`) is displayed during server polling to provide visual feedback without flooding the terminal.
- **Path sanitisation on client output filenames** — downloaded filenames are sanitised before writing to disk to prevent directory traversal in received file names.
- **`SIGPIPE` suppression on servers** — both servers ignore `SIGPIPE` signals so unexpected client/S1 disconnects do not crash the server process.
- **Transparent runtime logging on servers** — Server 1 log lines narrate the full orchestration flow at every step. Server 2 log lines explicitly state every request originated from Server 1.
- **Windows platform support** — Winsock 2, `GetCurrentDirectoryA`, Windows ANSI VT mode, and `.exe` binary naming are all handled transparently via compile-time platform macros.

---

## Implemented Scenarios & Use Cases

### Core File Distribution Scenarios (Original Problem Statement)

| # | Scenario | Description | Response to Client |
|---|---|---|---|
| 1 | **File identical on both servers** | Both S1 and S2 have the file with byte-for-byte identical content. S1 compares with `memcmp`. | One file sent — `status=1` + content |
| 2 | **File divergent on both servers** | Both S1 and S2 have the file but content differs (simulating replication lag). S1 detects mismatch. | Two files sent — `status=2` + S1 content + S2 content |
| 3 | **File on Server 1 only** | S1 has the file; S2 reports not found. S1 serves its own copy directly. | One file sent — `status=1` + S1 content |
| 4 | **File on Server 2 only** | S2 has the file; S1 does not. S1 proxies S2's copy to client. | One file sent — `status=1` + S2 content |
| 5 | **File not found on either server** | Neither server has the file. S1 responds with not-found status. | `status=0` — no file data |

### Extended Interactive Client Scenarios

| # | Scenario | Description |
|---|---|---|
| 6 | **Download — identical copy** | Client receives one file saved as `client_files/received_<name>` |
| 7 | **Download — divergent copies** | Client receives two files: `client_files/received1_<name>` (S1) and `client_files/received2_<name>` (S2) |
| 8 | **File size — identical on both** | Displays: `Found on both Server 1 and Server 2. Both copies are identical in size → N bytes` |
| 9 | **File size — divergent sizes** | Displays both sizes: `Server 1: N1 bytes / Server 2: N2 bytes` |
| 10 | **File size — one server only** | Displays: `Found on Server 1 only. Size: N bytes` (or Server 2) |
| 11 | **Create — on both servers** | File created on S1 locally and forwarded to S2 via Server 1. Per-server result displayed. |
| 12 | **Create — on Server 1 only** | File created on S1; S2 step skipped (status shown as `(skipped)`). |
| 13 | **Create — on Server 2 only** | S1 creation skipped; forwarded to S2 only. |
| 14 | **Create — with multi-line content** | User types content line by line; lone `.` ends input. Full content sent in one request. |
| 15 | **Create — empty file** | User selects `n` at content prompt. Zero-byte file created. |
| 16 | **List — both servers** | Files from S1 and S2 listed in separate groups. Matching name+size pairs annotated `(identical)`. |
| 17 | **List — single server** | Only the chosen server's file list is displayed. |
| 18 | **View — single copy** | File metadata (location, size) and content with line numbers displayed. |
| 19 | **View — divergent copies** | Both Server 1 and Server 2 content blocks shown with line numbers. |
| 20 | **Delete — found on both** | Dynamic menu offers: Both / Server 1 only / Server 2 only. |
| 21 | **Delete — found on one server** | Dynamic menu offers only the server where the file exists. |
| 22 | **Delete — not found** | Error message displayed; user prompted again. |
| 23 | **Exit — graceful shutdown** | `OP_SHUTDOWN` sent to S1, forwarded to S2. Client polls until both offline, then exits. |
| 24 | **Startup — both servers already running** | Warning about possible unexpected previous closure; proceeds directly to main menu. |
| 25 | **Startup — both servers offline** | Both server terminals auto-launched; client polls until both online. |
| 26 | **Startup — S1 up, S2 down** | Only S2 terminal launched. |
| 27 | **Startup — S2 up, S1 down** | S1 terminal launched; once S1 up, S1 checks S2 status via `OP_PING`. |
| 28 | **`!cancel` at filename prompt** | Operation aborted, returns to main menu. No server request sent. |
| 29 | **`!cancel` mid content-entry** | Multi-line content discarded, returns to main menu. |
| 30 | **Return to main menu from sub-menu** | All sub-menus have an `r. Return to main menu` option. |

### Server Auto-Recovery Scenarios

| # | Scenario | Recovery Flow |
|---|---|---|
| 31 | **S2 goes offline mid-session** | `OP_PING` reaches S1; S1 reports `s2_up=0`. S2 terminal relaunched. Client waits. Request resumes automatically. |
| 32 | **S1 goes offline, S2 still up** | Phase 1: S1 terminal relaunched, wait for S1. Phase 2: `OP_PING` via S1 confirms S2 still up. No S2 terminal launched. Request resumes. |
| 33 | **S1 goes offline, S2 also offline** | Phase 1: S1 relaunched, wait. Phase 2: S1 reports S2 offline. S2 terminal relaunched, wait. Both confirmed online. Request resumes. |
| 34 | **Both servers go offline simultaneously** | Same as Scenario 33 — treated as S1-down case; S2 status unknown until S1 recovers. |
| 35 | **Mid-request TCP disconnect** | `recv` failure mid-stream caught. Recovery engine triggered. Full request retried after recovery. |
| 36 | **Recovery timeout** | If server does not respond within 100 × 3s = 5 minutes, user is shown manual start commands and the operation is aborted cleanly. |

---

## Corner Cases & Edge Scenarios Handled

| # | Corner Case | Where Handled | How Handled |
|---|---|---|---|
| 1 | **Directory traversal in filename** (`../../etc/passwd`) | `server1.c` `build_fullpath`, `server2.c` `build_fullpath` | Any filename containing `..` or starting with `/` is rejected before any file operation; connection closed. |
| 2 | **Absolute path injection** (`/etc/passwd`) | `server1.c` `build_fullpath`, `server2.c` `build_fullpath` | Filenames starting with `/` are rejected immediately. |
| 3 | **Empty filename submitted** | `client.c` all command handlers | Empty string check after `read_line`; `print_err` shown, operation aborted without sending a request. |
| 4 | **`!cancel` token at any input stage** | `client.c` `is_cancel()` | Compared against `CANCEL_TOKEN` (`"!cancel"`) after every `read_line` call. Frees any allocated buffers and returns to main menu. |
| 5 | **`!cancel` mid multi-line content entry** | `client.c` `cmd_create` content loop | `is_cancel` check on every line. Allocated content buffer freed before return. |
| 6 | **Server 2 unreachable during Server 1 operation** | `server1.c` `s2_open()` | Returns `-1`; each handler treats S2 as not-found / skipped gracefully without crashing. |
| 7 | **Client disconnects mid-send (SIGPIPE)** | `server1.c` and `server2.c` main | `signal(SIGPIPE, SIG_IGN)` — broken pipe does not kill the server; `send` returns error which is ignored or checked. |
| 8 | **Partial TCP reads** | All three components `recv_all` / `send_all` | Loop until exactly `n` bytes received/sent, handling `EINTR` and partial delivery. |
| 9 | **Large file content** | `server1.c` `read_file_to_buf`, `client.c` `cmd_create` | Dynamic `malloc` sized by `fstat` (server) or `realloc`-growing buffer (client). Fixed buffer never used for content. |
| 10 | **`malloc` failure (out of memory)** | `client.c` content buffer allocation | `NULL` check after every `malloc`/`realloc`; `print_err("Out of memory.")` and early return. |
| 11 | **Two server launch scripts overwriting each other** | `client.c` `write_launch_script` | Scripts are named `/tmp/dfs_launch_<PID>_s1.sh` and `/tmp/dfs_launch_<PID>_s2.sh` — unique per server, no race condition. |
| 12 | **`osascript` quoting failures with spaces in path** | `client.c` `write_launch_script` | Shell command written to a temp script; `osascript` executes the script by path, never embeds the command string. `cd "path with spaces"` uses double quotes inside the script. |
| 13 | **Server port already in use on startup** | `server1.c` and `server2.c` `main` | `SO_REUSEADDR` socket option set before `bind` to allow quick restart after ungraceful shutdown. |
| 14 | **Server 2 status unknown when Server 1 is down** | `client.c` `ensure_servers_up` | Two-phase recovery: recover S1 first, then ask S1 via `OP_PING` to learn S2's true status. Direct client→S2 contact never occurs. |
| 15 | **Client cannot reach Server 1 mid-request (recv failure)** | `client.c` all command retry blocks | `recv_u32` / `recv_all` return value checked; on failure `print_warn("Lost connection mid-request")` and `ensure_servers_up` triggered, followed by `goto` retry. |
| 16 | **Recovery timeout — server never comes back** | `client.c` `wait_for_servers` | After 100 failed polls, returns `0`; `ensure_servers_up` prints manual startup commands and returns `0` to abort the operation cleanly. |
| 17 | **Delete — file found nowhere** | `client.c` `cmd_delete` Phase 1 | `s1_found=0 && s2_found=0` → `print_err` shown; function returns without entering Phase 2. |
| 18 | **Delete — server drops between Phase 1 and Phase 2** | `client.c` `cmd_delete` | `send_all` return value checked after user selects target; failure triggers recovery and full `goto do_delete_check` retry. |
| 19 | **File created with zero content** | `server1.c` `handle_create`, `server2.c` `handle_create` | `content_len=0` is a valid case; `write()` called with zero bytes creates an empty file correctly. |
| 20 | **List on empty directory** | `server1.c` `handle_list`, `server2.c` `handle_list` | `opendir` succeeds, loop finds no entries; `count=0` sent. Client displays `(no files)` without error. |
| 21 | **Output filename with path separators** (`dir/file.txt`) | `client.c` `sanitize_name` | `/` and `\` replaced with `_` in the local save filename, preventing writes outside `client_files/`. |
| 22 | **Non-TTY output (piped / redirected)** | `client.c` main | `isatty(STDOUT_FILENO)` check on startup; colour codes disabled when output is not a terminal. |
| 23 | **Windows without VT support** | `client.c` Windows init block | `GetStdHandle` + `SetConsoleMode` with `ENABLE_VIRTUAL_TERMINAL_PROCESSING`; graceful fallback if VT unavailable. |
| 24 | **Invalid menu choice** | `client.c` all `read_choice` call sites | Unrecognised character → `print_err("Invalid choice.")` and return to current menu level. |
| 25 | **Shutdown when a server is already offline** | `client.c` `do_shutdown_and_exit` | Polls until both servers stop responding; a server that was already offline trivially passes the "not responding" check. |

---

## Test Framework

C does not have a built-in testing framework, but there is a well-established ecosystem of unit testing libraries for C projects. For this codebase, **Unity** (by Throw The Switch) is the most appropriate choice — it is a lightweight, single-file C unit testing framework with no external dependencies, making it ideal for embedded and systems C code.

### Unit Test Cases

All test files are located in the `test/` directory and can be run with `make test`.

#### `test/test_pathsanitize.c` — Path Sanitisation (`build_fullpath`)

- Directory traversal via `..` is rejected
- Absolute path starting with `/` is rejected
- A valid filename produces the correctly joined path
- An empty filename is rejected

#### `test/test_sanitize_name.c` — Client Output Filename Sanitisation (`sanitize_name`)

- Forward slash `/` in a received filename is replaced with `_`
- Backslash `\` in a received filename is replaced with `_`
- A clean filename with no separators is left unchanged

#### `test/test_cancel_token.c` — Escape Token Detection (`is_cancel`)

- Exact token `!cancel` is detected as a cancel signal
- A normal filename is not treated as cancel
- An empty string is not treated as cancel
- Partial or extended strings that resemble the token are not cancel

#### `test/test_buffers_equal.c` — File Content Comparison (`buffers_equal`)

- Two buffers with byte-for-byte identical content are equal
- Two buffers of the same length but different bytes are not equal
- Two buffers of different lengths are not equal
- Two zero-length empty buffers are considered equal

#### `test/test_server_status_code.c` — Server Status Code Mapping (`server_status_code`)

- Both servers online maps to status code `3`
- Server 1 online, Server 2 offline maps to status code `1`
- Server 1 offline with Server 2 reported online maps to `0` (S1 is the gatekeeper; its report is unreachable)
- Both servers offline maps to status code `0`

#### `test/test_file_identical.c` — Identical File Detection Across Servers (`file_is_identical_in_list`)

- A file with matching name and size is found as identical
- A file with matching name but different size is not identical
- A file with different name but matching size is not identical
- A file absent from the other list entirely is not identical
- An empty other list always returns not identical
- A match on the second entry (not first) in the other list is correctly found

#### `test/test_server_ready.c` — Server Polling Readiness Guard (`wait_for_servers` logic)

- A server that is not needed and is offline is considered ready (no-wait)
- A server that is not needed and is online is considered ready
- A server that is needed but still offline is not ready (polling continues)
- A server that is needed and is online is ready (polling terminates)

#### `test/test_recv_length_guard.c` — Wire Protocol Length Validation (`recv_string` guard)

- Zero-length frame is rejected
- A normal filename length within all bounds is accepted
- A length exactly equal to `MAX_PATH_LEN` is accepted (boundary)
- A length one over `MAX_PATH_LEN` is rejected
- A length equal to the buffer capacity is rejected (would overflow null terminator)
- A length one below the buffer capacity is accepted (largest safe value)

#### `test/test_file_size_guard.c` — File Size Overflow Guard (`read_file_to_buf` guard)

- Negative file size (filesystem error) is rejected
- Zero size (empty file) is accepted
- A normal file size is accepted
- A size exactly equal to `UINT32_MAX` is accepted (largest wire-safe value)
- A size of `UINT32_MAX + 1` is rejected (would truncate on cast to `uint32_t`)

#### `test/test_target_label.c` — Target-to-Label Enum Mappings (client + server1)

- Client command target `0` maps to `"both servers"`
- Client command target `1` maps to `"Server 1"`
- Client command target `2` maps to `"Server 2"`
- Server create/list target `0` maps to `"both servers"`
- Server create/list target `1` maps to `"SERVER1 only"`
- Server create/list target `2` maps to `"SERVER2 only"`
- Delete scope `0` maps to `"both servers"`
- Delete scope `1` maps to `"SERVER1 only"`
- Delete scope `2` maps to `"SERVER2 only"`
- Delete scope out-of-range value maps to `"cancelled"`

> **Note:** Runtime integration testing covering all menu options, all five file distribution scenarios, all three recovery scenarios, and all edge cases has been completed manually by the team. The Unity test suite covers the pure-logic unit-testable functions and is intended for automated regression testing.
