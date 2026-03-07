/*
 * client.c — Interactive Distributed File System Client
 * -------------------------------------------------------
 * Usage: ./bin/client [server1_ip] [server1_port]
 *        (defaults: 127.0.0.1  5001)
 *
 * Type  !cancel  at any filename/content prompt to abort and return to main menu.
 */

/* ---- Platform detection ---- */
#if defined(_WIN32) || defined(_WIN64)
  #define PLATFORM_WINDOWS 1
#else
  #define PLATFORM_WINDOWS 0
#endif

#if defined(__APPLE__)
  #define PLATFORM_MACOS 1
#else
  #define PLATFORM_MACOS 0
#endif

#if defined(__linux__)
  #define PLATFORM_LINUX 1
#else
  #define PLATFORM_LINUX 0
#endif

#if PLATFORM_WINDOWS
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef int socklen_t;
  #define close(s) closesocket(s)
  #define sleep(s) Sleep((s)*1000)
  #define usleep(us) Sleep((us)/1000)
#else
  #include <unistd.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <fcntl.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <ctype.h>

/* ---- Opcodes (must match server1.c / server2.c) ---- */
#define OP_PING     0x01
#define OP_RETRIEVE 0x02
#define OP_SIZE     0x03
#define OP_CREATE   0x04
#define OP_LIST     0x05
#define OP_DELETE   0x06
#define OP_SHUTDOWN 0x07

/* CREATE / LIST targets */
#define TARGET_BOTH 0
#define TARGET_S1   1
#define TARGET_S2   2

#define MAX_PATH_LEN   4096
#define CLIENT_FILES   "client_files"
#define CANCEL_TOKEN   "!cancel"
#define SERVER1_PORT   5001
#define SERVER2_PORT   5002
#define POLL_INTERVAL  3      /* seconds between server polls */
#define POLL_MAX       100    /* maximum poll attempts */

/* ---- ANSI colour codes (disabled on Windows unless VT enabled) ---- */
#define COL_RESET   "\033[0m"
#define COL_BOLD    "\033[1m"
#define COL_RED     "\033[31m"
#define COL_GREEN   "\033[32m"
#define COL_YELLOW  "\033[33m"
#define COL_CYAN    "\033[36m"
#define COL_WHITE   "\033[37m"
#define COL_BWHITE  "\033[97m"

static int g_use_color = 1;

/* ---- Global server info ---- */
static char g_server1_ip[64] = "127.0.0.1";
static int  g_server1_port   = SERVER1_PORT;

/* ==========================================================
 * UTILITY: Terminal helpers
 * ========================================================== */

static void print_separator(void) {
    if (g_use_color) printf(COL_CYAN);
    printf("─────────────────────────────────────────────────────\n");
    if (g_use_color) printf(COL_RESET);
}

static void print_header(const char *title) {
    printf("\n");
    print_separator();
    if (g_use_color) printf(COL_BOLD COL_BWHITE);
    printf("  %s\n", title);
    if (g_use_color) printf(COL_RESET);
    print_separator();
}

static void print_ok(const char *msg) {
    if (g_use_color) printf(COL_GREEN "  ✔  " COL_RESET);
    else printf("  [OK]  ");
    printf("%s\n", msg);
}

static void print_err(const char *msg) {
    if (g_use_color) printf(COL_RED "  ✘  " COL_RESET);
    else printf(" [ERR]  ");
    printf("%s\n", msg);
}

static void print_warn(const char *msg) {
    if (g_use_color) printf(COL_YELLOW "  ⚠  " COL_RESET);
    else printf("[WARN]  ");
    printf("%s\n", msg);
}

static void print_info(const char *msg) {
    if (g_use_color) printf(COL_CYAN "  ℹ  " COL_RESET);
    else printf("[INFO]  ");
    printf("%s\n", msg);
}

/* ---- Spinner animation (prints in place) ---- */
static const char spinner_chars[] = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏";
static int spinner_idx = 0;

static void spinner_tick(const char *msg) {
#if PLATFORM_WINDOWS
    printf("\r  [%c]  %s", "|/-\\"[spinner_idx % 4], msg);
#else
    /* UTF-8 spinner: each char is 3 bytes */
    int idx = (spinner_idx % 10) * 3;
    printf("\r  %c%c%c  %s", spinner_chars[idx], spinner_chars[idx+1], spinner_chars[idx+2], msg);
#endif
    fflush(stdout);
    spinner_idx++;
}

static void spinner_done(void) {
    printf("\r                                                      \r");
    fflush(stdout);
}

/* ---- Read a trimmed line from stdin ---- */
static int read_line(const char *prompt, char *buf, size_t cap) {
    if (g_use_color) printf(COL_YELLOW "  ▶  " COL_RESET);
    else printf("  >  ");
    printf("%s", prompt);
    fflush(stdout);

    if (!fgets(buf, (int)cap, stdin)) return -1;
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
    return 0;
}

/* ---- Check if user typed the cancel token ---- */
static int is_cancel(const char *s) {
    return (strcmp(s, CANCEL_TOKEN) == 0);
}

/* ---- Lowercase single-char menu choice from a prompt ---- */
static char read_choice(const char *prompt) {
    char buf[16];
    if (read_line(prompt, buf, sizeof(buf)) != 0) return 0;
    if (strlen(buf) == 0) return 0;
    return (char)tolower((unsigned char)buf[0]);
}

/* ==========================================================
 * UTILITY: Socket helpers
 * ========================================================== */

#if PLATFORM_WINDOWS
static void init_winsock(void) {
    WSADATA wd;
    WSAStartup(MAKEWORD(2,2), &wd);
}
#endif

static int recv_all(int sock, void *buf, size_t n) {
    size_t got = 0;
    char *p = (char *)buf;
    while (got < n) {
        ssize_t r = recv(sock, p + got, (int)(n - got), 0);
        if (r == 0) return -1;
        if (r < 0) {
#if PLATFORM_WINDOWS
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
            return -1;
        }
        got += (size_t)r;
    }
    return 0;
}

static int send_all(int sock, const void *buf, size_t n) {
    size_t sent = 0;
    const char *p = (const char *)buf;
    while (sent < n) {
        ssize_t s = send(sock, p + sent, (int)(n - sent), 0);
        if (s < 0) {
#if PLATFORM_WINDOWS
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
            return -1;
        }
        sent += (size_t)s;
    }
    return 0;
}

static int send_u32(int sock, uint32_t val) {
    uint32_t n = htonl(val);
    return send_all(sock, &n, sizeof(n));
}

static int recv_u32(int sock, uint32_t *out) {
    uint32_t n = 0;
    if (recv_all(sock, &n, sizeof(n)) != 0) return -1;
    *out = ntohl(n);
    return 0;
}

static int send_string(int sock, const char *str) {
    uint32_t len = (uint32_t)strlen(str);
    if (send_u32(sock, len) != 0) return -1;
    return send_all(sock, str, len);
}

/* ---- Connect to SERVER1 with short timeout ---- */
static int connect_to_server1(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)g_server1_port);
    if (inet_pton(AF_INET, g_server1_ip, &a.sin_addr) != 1) {
        close(fd); return -1;
    }

#if !PLATFORM_WINDOWS
    /* Set non-blocking for connect so we get a fast timeout */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = connect(fd, (struct sockaddr *)&a, sizeof(a));
    if (rc != 0 && errno != EINPROGRESS) { close(fd); return -1; }

    /* Wait up to 2 seconds */
    fd_set wset;
    FD_ZERO(&wset); FD_SET(fd, &wset);
    struct timeval tv = {2, 0};
    int sel = select(fd + 1, NULL, &wset, NULL, &tv);
    if (sel <= 0) { close(fd); return -1; }

    /* Check for actual connect error */
    int err = 0; socklen_t elen = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);
    if (err != 0) { close(fd); return -1; }

    /* Restore blocking */
    fcntl(fd, F_SETFL, flags);
#else
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        close(fd); return -1;
    }
#endif
    return fd;
}

/* ==========================================================
 * UTILITY: File system helpers
 * ========================================================== */

static void ensure_client_files_dir(void) {
#if PLATFORM_WINDOWS
    CreateDirectoryA(CLIENT_FILES, NULL);
#else
    mkdir(CLIENT_FILES, 0755);
#endif
}

static void sanitize_name(const char *in, char *out, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 1 < cap; i++) {
        char c = in[i];
        if (c == '/' || c == '\\') c = '_';
        out[j++] = c;
    }
    out[j] = '\0';
}

static int write_file(const char *path, const uint8_t *buf, uint32_t len) {
#if PLATFORM_WINDOWS
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD written;
    WriteFile(h, buf, len, &written, NULL);
    CloseHandle(h);
    return 0;
#else
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) return -1;
    uint32_t tot = 0;
    while (tot < len) {
        ssize_t w = write(fd, buf + tot, len - tot);
        if (w < 0) { if (errno == EINTR) continue; close(fd); return -1; }
        tot += (uint32_t)w;
    }
    close(fd);
    return 0;
#endif
}

/* ==========================================================
 * UTILITY: Terminal launching (cross-platform)
 * ========================================================== */

/* Detect the terminal emulator being used */
typedef enum {
    TERM_UNKNOWN,
    TERM_ITERM2,
    TERM_APPLE_TERMINAL,
    TERM_GNOME_TERMINAL,
    TERM_KONSOLE,
    TERM_XFCE_TERMINAL,
    TERM_XTERM,
    TERM_WINDOWS_CMD,
    TERM_WINDOWS_POWERSHELL,
    TERM_WSL
} TermType;

/* ---- Global terminal type (set once in main, used by auto-recovery everywhere) ---- */
static TermType g_ttype = TERM_UNKNOWN;

static TermType detect_terminal(void) {
#if PLATFORM_WINDOWS
    /* Check if running under WSL or native Windows */
    const char *wsl = getenv("WSL_DISTRO_NAME");
    if (wsl && strlen(wsl) > 0) return TERM_WSL;
    const char *term_prog = getenv("WT_SESSION");
    if (term_prog) return TERM_WINDOWS_POWERSHELL;
    return TERM_WINDOWS_CMD;
#else
    const char *term_prog = getenv("TERM_PROGRAM");
    if (term_prog) {
        if (strstr(term_prog, "iTerm")) return TERM_ITERM2;
        if (strstr(term_prog, "Apple_Terminal")) return TERM_APPLE_TERMINAL;
    }
    const char *term = getenv("TERM");
    if (term) {
        if (strstr(term, "xterm")) return TERM_XTERM;
    }
#if PLATFORM_LINUX
    /* Check available terminal emulators */
    if (system("which gnome-terminal > /dev/null 2>&1") == 0) return TERM_GNOME_TERMINAL;
    if (system("which konsole > /dev/null 2>&1") == 0)        return TERM_KONSOLE;
    if (system("which xfce4-terminal > /dev/null 2>&1") == 0) return TERM_XFCE_TERMINAL;
    if (system("which xterm > /dev/null 2>&1") == 0)          return TERM_XTERM;
#endif
    /* Fallback for macOS */
    return TERM_APPLE_TERMINAL;
#endif
}

/*
 * Launch a new terminal window running the given shell command.
 * cmd: full shell command to execute inside the new terminal.
 * Returns 0 on success, -1 on failure.
 */
/*
 * Write a temporary shell script, make it executable, and return its path.
 * label: short identifier appended to the filename (e.g. "s1", "s2") so that
 * two concurrent launches don't overwrite each other's script.
 * Returns 0 on success, -1 on failure.
 */
static int write_launch_script(char *script_path, size_t path_cap,
                                const char *label, const char *cmd) {
    snprintf(script_path, path_cap, "/tmp/dfs_launch_%d_%s.sh", (int)getpid(), label);
    FILE *f = fopen(script_path, "w");
    if (!f) return -1;
    fprintf(f, "#!/bin/bash\n%s\nexec bash\n", cmd);
    fclose(f);
    chmod(script_path, 0755);
    return 0;
}

static int launch_terminal(const char *label, const char *cmd, TermType ttype) {
    char buf[8192];

#if PLATFORM_WINDOWS
    (void)label;
    if (ttype == TERM_WINDOWS_CMD || ttype == TERM_UNKNOWN) {
        snprintf(buf, sizeof(buf), "start cmd.exe /k \"%s\"", cmd);
    } else if (ttype == TERM_WINDOWS_POWERSHELL) {
        snprintf(buf, sizeof(buf),
                 "start powershell.exe -NoExit -Command \"%s\"", cmd);
    } else if (ttype == TERM_WSL) {
        snprintf(buf, sizeof(buf), "start wsl.exe -e bash -c \"%s; exec bash\"", cmd);
    } else {
        snprintf(buf, sizeof(buf), "start cmd.exe /k \"%s\"", cmd);
    }
    return (system(buf) == 0) ? 0 : -1;
#else
    /*
     * Write a uniquely-named temporary shell script per launch so that two
     * back-to-back launches don't overwrite each other's script before the
     * terminal emulator has a chance to read it.
     */
    char script_path[512];
    if (write_launch_script(script_path, sizeof(script_path), label, cmd) != 0) return -1;

    if (ttype == TERM_ITERM2) {
        /* AppleScript: open a new iTerm2 window and run the script */
        snprintf(buf, sizeof(buf),
            "osascript"
            " -e 'tell application \"iTerm2\"'"
            " -e 'set newWin to (create window with default profile)'"
            " -e 'tell current session of newWin'"
            " -e 'write text \"%s\"'"
            " -e 'end tell'"
            " -e 'end tell'",
            script_path);
    } else if (ttype == TERM_APPLE_TERMINAL) {
        snprintf(buf, sizeof(buf),
            "osascript"
            " -e 'tell application \"Terminal\"'"
            " -e 'do script \"%s\"'"
            " -e 'end tell'",
            script_path);
    } else if (ttype == TERM_GNOME_TERMINAL) {
        snprintf(buf, sizeof(buf), "gnome-terminal -- bash \"%s\" &", script_path);
    } else if (ttype == TERM_KONSOLE) {
        snprintf(buf, sizeof(buf), "konsole -e bash \"%s\" &", script_path);
    } else if (ttype == TERM_XFCE_TERMINAL) {
        snprintf(buf, sizeof(buf), "xfce4-terminal -e \"bash '%s'\" &", script_path);
    } else {
        /* fallback: xterm */
        snprintf(buf, sizeof(buf), "xterm -e bash \"%s\" &", script_path);
    }
    return (system(buf) == 0) ? 0 : -1;
#endif
}

/* ==========================================================
 * UTILITY: Server status check
 * ========================================================== */

/*
 * Check both server statuses via OP_PING to SERVER1.
 * SERVER1 handles the ping and asks SERVER2 itself, then replies:
 *   [uint32_t s1_up=1][uint32_t s2_up]
 *
 * All knowledge of SERVER2 comes exclusively through SERVER1 —
 * the client never contacts SERVER2 directly.
 *
 * out_s1 / out_s2: 1=online, 0=offline (or unknown when S1 is down)
 */
static int check_server_status(int *out_s1, int *out_s2) {
    *out_s1 = 0;
    *out_s2 = 0;

    int fd = connect_to_server1();
    if (fd < 0) return 0;  /* S1 unreachable; S2 status unknown */

    uint8_t op = OP_PING;
    if (send_all(fd, &op, 1) != 0) { close(fd); return 0; }

    uint32_t s1_r = 0, s2_r = 0;
    if (recv_u32(fd, &s1_r) != 0 || recv_u32(fd, &s2_r) != 0) {
        close(fd); return 0;
    }
    close(fd);

    *out_s1 = (int)s1_r;
    *out_s2 = (int)s2_r;

    if (*out_s1 && *out_s2) return 3;
    if (*out_s1 && !*out_s2) return 1;
    return 0;
}

static void print_server_status(int s1, int s2) {
    printf("\n");
    if (g_use_color)
        printf("  Server 1 [:%d]  %s%s%s\n",
               SERVER1_PORT,
               s1 ? COL_GREEN : COL_RED,
               s1 ? "● ONLINE" : "○ OFFLINE",
               COL_RESET);
    else
        printf("  Server 1 [:%d]  %s\n", SERVER1_PORT, s1 ? "ONLINE" : "OFFLINE");

    if (g_use_color)
        printf("  Server 2 [:%d]  %s%s%s\n",
               SERVER2_PORT,
               s2 ? COL_GREEN : COL_RED,
               s2 ? "● ONLINE" : "○ OFFLINE",
               COL_RESET);
    else
        printf("  Server 2 [:%d]  %s\n", SERVER2_PORT, s2 ? "ONLINE" : "OFFLINE");
    printf("\n");
}

/* Forward declarations for functions defined further below */
static void launch_server_windows(int server_num, TermType ttype);
static int  wait_for_servers(int need_s1, int need_s2);

/*
 * Shared helper: launch one server, wait for it, print result.
 * Returns 1 on success, 0 on timeout.
 * need_s1 / need_s2 are passed to wait_for_servers so the spinner
 * correctly labels which server it is waiting for.
 */
static int recover_one_server(int server_num, int need_s1, int need_s2) {
    print_info(server_num == 1 ? "Launching Server 1 terminal..."
                               : "Launching Server 2 terminal...");
    launch_server_windows(server_num, g_ttype);
    printf("\n");
    print_info(server_num == 1 ? "Waiting for Server 1 to come online...\n"
                               : "Waiting for Server 2 to come online...\n");
    return wait_for_servers(need_s1, need_s2);
}

/*
 * Ensure both servers are online before a command executes.
 *
 * Recovery follows strict Client → Server1 → Server2 architecture:
 *   - Server 2 status is ALWAYS learned via Server 1's OP_PING response.
 *   - The client never contacts Server 2 directly.
 *
 * Special case — Server 1 is down:
 *   Phase 1: Launch Server 1, wait for it to come up.
 *   Phase 2: Once Server 1 is up, ask it (via OP_PING) whether Server 2
 *            is also running. If Server 2 is down too, launch it and wait.
 *
 * Returns 1 when both servers are confirmed online (safe to proceed).
 * Returns 0 if recovery times out (caller should abort the operation).
 *
 * context: human-readable label for the queued operation, e.g.
 *          "download 'a.txt'" — shown in the "Resuming:" message.
 */
static int ensure_servers_up(const char *context) {
    int s1, s2;
    check_server_status(&s1, &s2);
    if (s1 && s2) return 1;  /* fast path — nothing to do */

    printf("\n");
    print_warn("Server disruption detected while processing your request.");
    print_server_status(s1, s2);

    /* ------------------------------------------------------------------
     * Case A: Server 1 is down (s2 status is unknown at this point
     *         because we can only learn it through Server 1).
     * ------------------------------------------------------------------ */
    if (!s1) {
        print_warn("Server 1 is offline — recovering Server 1 first...");

        if (!recover_one_server(1, /*need_s1=*/1, /*need_s2=*/0)) {
            printf("\n");
            print_err("Server 1 could not be recovered within the timeout.");
            print_info("Please start Server 1 manually:");
            printf("    ./bin/server1 5001 127.0.0.1 5002 ./server1_files\n\n");
            return 0;
        }

        print_ok("Server 1 — back ONLINE");

        /* Now ask Server 1 about Server 2 (correct architecture: C→S1→S2) */
        check_server_status(&s1, &s2);

        if (!s2) {
            printf("\n");
            print_warn("Server 1 reports Server 2 is also offline — recovering Server 2...");
            print_server_status(s1, s2);

            if (!recover_one_server(2, /*need_s1=*/0, /*need_s2=*/1)) {
                printf("\n");
                print_err("Server 2 could not be recovered within the timeout.");
                print_info("Please start Server 2 manually:");
                printf("    ./bin/server2 5002 ./server2_files\n\n");
                return 0;
            }

            print_ok("Server 2 — back ONLINE");
        } else {
            print_ok("Server 2 — already ONLINE (confirmed via Server 1)");
        }

    /* ------------------------------------------------------------------
     * Case B: Server 1 is up but Server 2 is down.
     *         Standard single-server recovery.
     * ------------------------------------------------------------------ */
    } else {
        print_warn("Server 2 is offline — auto-recovering...");

        if (!recover_one_server(2, /*need_s1=*/0, /*need_s2=*/1)) {
            printf("\n");
            print_err("Server 2 could not be recovered within the timeout.");
            print_info("Please start Server 2 manually:");
            printf("    ./bin/server2 5002 ./server2_files\n\n");
            return 0;
        }

        print_ok("Server 2 — back ONLINE");
    }

    /* Final confirmation */
    check_server_status(&s1, &s2);
    if (!s1 || !s2) {
        print_err("Not all servers came back online. Cannot resume.");
        return 0;
    }

    printf("\n");
    if (g_use_color) printf(COL_BOLD COL_GREEN);
    if (context && context[0])
        printf("  All servers online. Resuming: %s\n", context);
    else
        printf("  All servers online. Resuming your request...\n");
    if (g_use_color) printf(COL_RESET);
    printf("\n");

    return 1;
}

/* ==========================================================
 * STARTUP: Launch server terminals and wait
 * ========================================================== */

static void launch_server_windows(int server_num, TermType ttype) {
    char cwd[2048];
#if PLATFORM_WINDOWS
    GetCurrentDirectoryA(sizeof(cwd), cwd);
    char cmd[4096];
    if (server_num == 1)
        snprintf(cmd, sizeof(cmd),
                 "cd /d \"%s\" && bin\\server1.exe 5001 127.0.0.1 5002 server1_files", cwd);
    else
        snprintf(cmd, sizeof(cmd),
                 "cd /d \"%s\" && bin\\server2.exe 5002 server2_files", cwd);
#else
    if (!getcwd(cwd, sizeof(cwd))) {
        snprintf(cwd, sizeof(cwd), ".");
    }
    char cmd[4096];
    /*
     * Use printf %q-style quoting: wrap the cwd in single quotes and escape
     * any literal single quotes inside it as '\'' so the shell handles spaces
     * and special characters safely inside the script body.
     */
    if (server_num == 1)
        snprintf(cmd, sizeof(cmd),
                 "cd \"%s\" && ./bin/server1 5001 127.0.0.1 5002 ./server1_files", cwd);
    else
        snprintf(cmd, sizeof(cmd),
                 "cd \"%s\" && ./bin/server2 5002 ./server2_files", cwd);
#endif

    char label[8];
    snprintf(label, sizeof(label), "s%d", server_num);

    if (launch_terminal(label, cmd, ttype) == 0)
        printf("  Launched Server %d terminal.\n", server_num);
    else
        printf("  WARNING: Could not auto-launch Server %d terminal.\n", server_num);
}

/*
 * Wait for servers to come online.
 * need_s1, need_s2: which servers we need to wait for.
 * Returns 1 when all needed servers are up, 0 on timeout.
 */
static int wait_for_servers(int need_s1, int need_s2) {
    char spin_msg[128];
    int attempts = 0;
    while (attempts < POLL_MAX) {
        int s1, s2;
        check_server_status(&s1, &s2);

        int s1_ready = !need_s1 || s1;
        int s2_ready = !need_s2 || s2;

        if (s1_ready && s2_ready) {
            spinner_done();
            return 1;
        }

        snprintf(spin_msg, sizeof(spin_msg),
                 "Waiting for servers... (attempt %d/%d)  %s%s",
                 attempts + 1, POLL_MAX,
                 (need_s1 && !s1) ? "[S1 waiting] " : "",
                 (need_s2 && !s2) ? "[S2 waiting] " : "");
        spinner_tick(spin_msg);

        sleep(POLL_INTERVAL);
        attempts++;
    }
    spinner_done();
    return 0;
}

/* ==========================================================
 * COMMAND: RETRIEVE (Download File)
 * ========================================================== */

static void cmd_retrieve(void) {
    print_header("Download File");
    printf("  Type " COL_YELLOW "!cancel" COL_RESET " at any prompt to return to main menu.\n\n");

    char path[MAX_PATH_LEN + 1];
    if (read_line("Enter file name: ", path, sizeof(path)) != 0) return;
    if (is_cancel(path)) { print_info("Cancelled."); return; }
    if (strlen(path) == 0) { print_err("File name cannot be empty."); return; }

    char context[MAX_PATH_LEN + 32];
    snprintf(context, sizeof(context), "download '%s'", path);

do_retrieve:
    if (!ensure_servers_up(context)) return;

    int fd = connect_to_server1();
    if (fd < 0) {
        print_err("Cannot connect to Server 1.");
        if (!ensure_servers_up(context)) return;
        goto do_retrieve;
    }

    uint8_t op = OP_RETRIEVE;
    send_all(fd, &op, 1);
    send_string(fd, path);

    uint32_t status = 0;
    if (recv_u32(fd, &status) != 0) {
        close(fd);
        print_warn("Lost connection to server mid-request.");
        if (!ensure_servers_up(context)) return;
        goto do_retrieve;
    }

    ensure_client_files_dir();
    char safe[MAX_PATH_LEN + 1];
    sanitize_name(path, safe, sizeof(safe));

    if (status == 0) {
        close(fd);
        char msg[512]; snprintf(msg, sizeof(msg), "File '%s' was NOT FOUND on any server.", path);
        print_err(msg);
        return;
    }

    if (status == 1) {
        uint32_t len = 0;
        recv_u32(fd, &len);
        uint8_t *buf = (len > 0) ? (uint8_t *)malloc(len) : NULL;
        if (len > 0 && buf) recv_all(fd, buf, len);
        close(fd);

        char outpath[MAX_PATH_LEN + 64];
        snprintf(outpath, sizeof(outpath), "%s/received_%s", CLIENT_FILES, safe);
        write_file(outpath, buf, len);
        free(buf);

        char msg[512];
        snprintf(msg, sizeof(msg), "File saved: %s  (%u bytes)", outpath, len);
        print_ok(msg);
        return;
    }

    if (status == 2) {
        uint32_t len1 = 0, len2 = 0;
        recv_u32(fd, &len1);
        uint8_t *buf1 = (len1 > 0) ? (uint8_t *)malloc(len1) : NULL;
        if (len1 > 0 && buf1) recv_all(fd, buf1, len1);
        recv_u32(fd, &len2);
        uint8_t *buf2 = (len2 > 0) ? (uint8_t *)malloc(len2) : NULL;
        if (len2 > 0 && buf2) recv_all(fd, buf2, len2);
        close(fd);

        char out1[MAX_PATH_LEN + 64], out2[MAX_PATH_LEN + 64];
        snprintf(out1, sizeof(out1), "%s/received1_%s", CLIENT_FILES, safe);
        snprintf(out2, sizeof(out2), "%s/received2_%s", CLIENT_FILES, safe);
        write_file(out1, buf1, len1);
        write_file(out2, buf2, len2);
        free(buf1); free(buf2);

        print_warn("File exists on BOTH servers but copies are DIFFERENT:");
        char msg[512];
        snprintf(msg, sizeof(msg), "Server 1 copy saved: %s  (%u bytes)", out1, len1);
        print_ok(msg);
        snprintf(msg, sizeof(msg), "Server 2 copy saved: %s  (%u bytes)", out2, len2);
        print_ok(msg);
        return;
    }

    close(fd);
    print_err("Unexpected response from server.");
}

/* ==========================================================
 * COMMAND: FILE SIZE
 * ========================================================== */

static void cmd_size(void) {
    print_header("File Size");
    printf("  Type " COL_YELLOW "!cancel" COL_RESET " at any prompt to return to main menu.\n\n");

    char path[MAX_PATH_LEN + 1];
    if (read_line("Enter file name: ", path, sizeof(path)) != 0) return;
    if (is_cancel(path)) { print_info("Cancelled."); return; }
    if (strlen(path) == 0) { print_err("File name cannot be empty."); return; }

    char context[MAX_PATH_LEN + 32];
    snprintf(context, sizeof(context), "file size of '%s'", path);

do_size:
    if (!ensure_servers_up(context)) return;

    int fd = connect_to_server1();
    if (fd < 0) {
        if (!ensure_servers_up(context)) return;
        goto do_size;
    }

    uint8_t op = OP_SIZE;
    send_all(fd, &op, 1);
    send_string(fd, path);

    uint32_t s1_found = 0, s1_size = 0, s2_found = 0, s2_size = 0;
    if (recv_u32(fd, &s1_found) != 0 || recv_u32(fd, &s1_size) != 0 ||
        recv_u32(fd, &s2_found) != 0 || recv_u32(fd, &s2_size) != 0) {
        close(fd);
        print_warn("Lost connection to server mid-request.");
        if (!ensure_servers_up(context)) return;
        goto do_size;
    }
    close(fd);

    printf("\n");
    print_separator();

    if (!s1_found && !s2_found) {
        char msg[256]; snprintf(msg, sizeof(msg), "File '%s' was NOT FOUND on any server.", path);
        print_err(msg);
        print_separator();
        return;
    }

    if (s1_found && s2_found) {
        if (s1_size == s2_size) {
            if (g_use_color) printf(COL_GREEN);
            printf("  Found on both Server 1 and Server 2.\n");
            printf("  Both copies are identical in size → %u byte%s\n", s1_size, s1_size == 1 ? "" : "s");
            if (g_use_color) printf(COL_RESET);
        } else {
            if (g_use_color) printf(COL_YELLOW);
            printf("  Found on both Server 1 and Server 2, sizes differ:\n");
            printf("    Server 1: %u byte%s\n", s1_size, s1_size == 1 ? "" : "s");
            printf("    Server 2: %u byte%s\n", s2_size, s2_size == 1 ? "" : "s");
            if (g_use_color) printf(COL_RESET);
        }
    } else if (s1_found) {
        if (g_use_color) printf(COL_CYAN);
        printf("  Found on Server 1 only.\n");
        printf("  Size: %u byte%s\n", s1_size, s1_size == 1 ? "" : "s");
        if (g_use_color) printf(COL_RESET);
    } else {
        if (g_use_color) printf(COL_CYAN);
        printf("  Found on Server 2 only.\n");
        printf("  Size: %u byte%s\n", s2_size, s2_size == 1 ? "" : "s");
        if (g_use_color) printf(COL_RESET);
    }

    print_separator();
}

/* ==========================================================
 * COMMAND: CREATE FILE
 * ========================================================== */

static void cmd_create(void) {
    print_header("Create File");

    printf("  Where would you like to create the file?\n\n");
    printf("    a. Both servers\n");
    printf("    b. Server 1 only\n");
    printf("    c. Server 2 only\n");
    printf("    r. Return to main menu\n");
    printf("    e. Exit\n\n");

    char choice = read_choice("Your choice: ");
    uint8_t target;
    if (choice == 'a') target = TARGET_BOTH;
    else if (choice == 'b') target = TARGET_S1;
    else if (choice == 'c') target = TARGET_S2;
    else if (choice == 'r') return;
    else if (choice == 'e') goto do_exit;
    else { print_err("Invalid choice."); return; }

    /* Get file name */
    char path[MAX_PATH_LEN + 1];
    if (read_line("\nEnter file name (with extension): ", path, sizeof(path)) != 0) return;
    if (is_cancel(path)) { print_info("Cancelled."); return; }
    if (strlen(path) == 0) { print_err("File name cannot be empty."); return; }

    /* Ask for content */
    char add_choice = read_choice("Add content to the file? [y/n]: ");
    uint8_t *content = NULL;
    uint32_t clen = 0;

    if (add_choice == 'y') {
        printf("  Enter file content below. Type a single '.' on its own line to finish.\n");
        printf("  (Type !cancel to abort)\n\n");

        /* Dynamically build content buffer */
        size_t buf_cap = 4096;
        char *cbuf = (char *)malloc(buf_cap);
        if (!cbuf) { print_err("Out of memory."); return; }
        cbuf[0] = '\0';
        size_t total = 0;

        char line[4096];
        while (1) {
            if (read_line("", line, sizeof(line)) != 0) break;
            if (is_cancel(line)) {
                free(cbuf);
                print_info("Cancelled.");
                return;
            }
            if (strcmp(line, ".") == 0) break;

            size_t llen = strlen(line);
            size_t needed = total + llen + 1 + 1;  /* +1 newline +1 null */
            while (needed > buf_cap) {
                buf_cap *= 2;
                char *nb = (char *)realloc(cbuf, buf_cap);
                if (!nb) { free(cbuf); print_err("Out of memory."); return; }
                cbuf = nb;
            }
            memcpy(cbuf + total, line, llen);
            total += llen;
            cbuf[total++] = '\n';
            cbuf[total] = '\0';
        }

        clen = (uint32_t)total;
        content = (uint8_t *)cbuf;
    }

    {
        const char *tgt_label = (target == TARGET_BOTH) ? "both servers"
                              : (target == TARGET_S1)   ? "Server 1"
                              :                           "Server 2";
        char context[MAX_PATH_LEN + 48];
        snprintf(context, sizeof(context), "create '%s' on %s", path, tgt_label);

do_create:
        if (!ensure_servers_up(context)) { free(content); return; }

        int fd = connect_to_server1();
        if (fd < 0) {
            if (!ensure_servers_up(context)) { free(content); return; }
            goto do_create;
        }

        uint8_t op = OP_CREATE;
        send_all(fd, &op, 1);
        send_all(fd, &target, 1);
        send_string(fd, path);
        send_u32(fd, clen);
        if (clen > 0) send_all(fd, content, clen);

        uint32_t s1_st = 0, s2_st = 0;
        if (recv_u32(fd, &s1_st) != 0 || recv_u32(fd, &s2_st) != 0) {
            close(fd);
            print_warn("Lost connection to server mid-request.");
            if (!ensure_servers_up(context)) { free(content); return; }
            goto do_create;
        }
        close(fd);
        free(content);

        printf("\n");
        const char *labels[] = {"(skipped)", "Success", "FAILED"};
        if (target == TARGET_BOTH || target == TARGET_S1) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Server 1 create: %s", labels[s1_st < 3 ? s1_st : 2]);
            if (s1_st == 1) print_ok(msg); else if (s1_st == 0) print_info(msg); else print_err(msg);
        }
        if (target == TARGET_BOTH || target == TARGET_S2) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Server 2 create: %s", labels[s2_st < 3 ? s2_st : 2]);
            if (s2_st == 1) print_ok(msg); else if (s2_st == 0) print_info(msg); else print_err(msg);
        }
        return;
    }

do_exit:
    exit(0);
}

/* ==========================================================
 * COMMAND: LIST FILES
 * ========================================================== */

/* File entry for merge/comparison */
typedef struct {
    char name[512];
    uint32_t size;
} FileEntry;

static void print_file_list(FileEntry *s1_files, int s1_count,
                             FileEntry *s2_files, int s2_count,
                             uint8_t target) {
    printf("\n");
    print_separator();

    if (target == TARGET_BOTH || target == TARGET_S1) {
        if (g_use_color) printf(COL_BOLD COL_CYAN);
        printf("  Server 1 Files:\n");
        if (g_use_color) printf(COL_RESET);

        if (s1_count == 0) {
            print_info("  (no files)");
        } else {
            for (int i = 0; i < s1_count; i++) {
                /* Check if identical copy exists on s2 */
                int identical = 0;
                if (target == TARGET_BOTH) {
                    for (int j = 0; j < s2_count; j++) {
                        if (strcmp(s1_files[i].name, s2_files[j].name) == 0 &&
                            s1_files[i].size == s2_files[j].size) {
                            identical = 1;
                            break;
                        }
                    }
                }
                if (g_use_color) printf(COL_WHITE);
                printf("    %-30s  %6u bytes", s1_files[i].name, s1_files[i].size);
                if (identical) {
                    if (g_use_color) printf(COL_GREEN " (identical)" COL_RESET);
                    else printf(" (identical)");
                }
                printf("\n");
                if (g_use_color) printf(COL_RESET);
            }
        }
        printf("\n");
    }

    if (target == TARGET_BOTH || target == TARGET_S2) {
        if (g_use_color) printf(COL_BOLD COL_CYAN);
        printf("  Server 2 Files:\n");
        if (g_use_color) printf(COL_RESET);

        if (s2_count == 0) {
            print_info("  (no files)");
        } else {
            for (int j = 0; j < s2_count; j++) {
                int identical = 0;
                if (target == TARGET_BOTH) {
                    for (int i = 0; i < s1_count; i++) {
                        if (strcmp(s2_files[j].name, s1_files[i].name) == 0 &&
                            s2_files[j].size == s1_files[i].size) {
                            identical = 1;
                            break;
                        }
                    }
                }
                if (g_use_color) printf(COL_WHITE);
                printf("    %-30s  %6u bytes", s2_files[j].name, s2_files[j].size);
                if (identical) {
                    if (g_use_color) printf(COL_GREEN " (identical)" COL_RESET);
                    else printf(" (identical)");
                }
                printf("\n");
                if (g_use_color) printf(COL_RESET);
            }
        }
    }

    print_separator();
}

static void cmd_list(void) {
    print_header("List Files");

    printf("  Which server files would you like to list?\n\n");
    printf("    a. Both servers\n");
    printf("    b. Server 1 only\n");
    printf("    c. Server 2 only\n");
    printf("    r. Return to main menu\n");
    printf("    e. Exit\n\n");

    char choice = read_choice("Your choice: ");
    uint8_t target;
    if (choice == 'a') target = TARGET_BOTH;
    else if (choice == 'b') target = TARGET_S1;
    else if (choice == 'c') target = TARGET_S2;
    else if (choice == 'r') return;
    else if (choice == 'e') exit(0);
    else { print_err("Invalid choice."); return; }

    {
        const char *tgt_label = (target == TARGET_BOTH) ? "both servers"
                              : (target == TARGET_S1)   ? "Server 1"
                              :                           "Server 2";
        char context[64];
        snprintf(context, sizeof(context), "list files from %s", tgt_label);

do_list:
        if (!ensure_servers_up(context)) return;

        int fd = connect_to_server1();
        if (fd < 0) {
            if (!ensure_servers_up(context)) return;
            goto do_list;
        }

        uint8_t op = OP_LIST;
        send_all(fd, &op, 1);
        send_all(fd, &target, 1);

        /* Receive s1 list */
        uint32_t s1_count = 0;
        if (recv_u32(fd, &s1_count) != 0) {
            close(fd);
            print_warn("Lost connection to server mid-request.");
            if (!ensure_servers_up(context)) return;
            goto do_list;
        }
        FileEntry *s1_files = NULL;
        if (s1_count > 0) {
            s1_files = (FileEntry *)malloc(s1_count * sizeof(FileEntry));
            for (uint32_t i = 0; i < s1_count; i++) {
                uint32_t nlen = 0;
                recv_u32(fd, &nlen);
                if (nlen > 0 && nlen < sizeof(s1_files[i].name)) {
                    recv_all(fd, s1_files[i].name, nlen);
                    s1_files[i].name[nlen] = '\0';
                }
                recv_u32(fd, &s1_files[i].size);
            }
        }

        /* Receive s2 list */
        uint32_t s2_count = 0;
        recv_u32(fd, &s2_count);
        FileEntry *s2_files = NULL;
        if (s2_count > 0) {
            s2_files = (FileEntry *)malloc(s2_count * sizeof(FileEntry));
            for (uint32_t j = 0; j < s2_count; j++) {
                uint32_t nlen = 0;
                recv_u32(fd, &nlen);
                if (nlen > 0 && nlen < sizeof(s2_files[j].name)) {
                    recv_all(fd, s2_files[j].name, nlen);
                    s2_files[j].name[nlen] = '\0';
                }
                recv_u32(fd, &s2_files[j].size);
            }
        }

        close(fd);
        print_file_list(s1_files, (int)s1_count, s2_files, (int)s2_count, target);
        free(s1_files);
        free(s2_files);
    }
}

/* ==========================================================
 * COMMAND: VIEW FILE
 * ========================================================== */

static void cmd_view(void) {
    print_header("View File");
    printf("  Type " COL_YELLOW "!cancel" COL_RESET " at any prompt to return to main menu.\n\n");

    char path[MAX_PATH_LEN + 1];
    if (read_line("Enter file name: ", path, sizeof(path)) != 0) return;
    if (is_cancel(path)) { print_info("Cancelled."); return; }
    if (strlen(path) == 0) { print_err("File name cannot be empty."); return; }

    char context[MAX_PATH_LEN + 32];
    snprintf(context, sizeof(context), "view '%s'", path);

do_view:
    if (!ensure_servers_up(context)) return;

    /* Use SIZE to get metadata */
    int fd = connect_to_server1();
    if (fd < 0) {
        if (!ensure_servers_up(context)) return;
        goto do_view;
    }
    {
        uint8_t op = OP_SIZE;
        send_all(fd, &op, 1);
        send_string(fd, path);
    }
    uint32_t s1_found = 0, s1_size = 0, s2_found = 0, s2_size = 0;
    if (recv_u32(fd, &s1_found) != 0 || recv_u32(fd, &s1_size) != 0 ||
        recv_u32(fd, &s2_found) != 0 || recv_u32(fd, &s2_size) != 0) {
        close(fd);
        print_warn("Lost connection to server mid-request.");
        if (!ensure_servers_up(context)) return;
        goto do_view;
    }
    close(fd);

    /* Now retrieve content */
    fd = connect_to_server1();
    if (fd < 0) {
        if (!ensure_servers_up(context)) return;
        goto do_view;
    }
    {
        uint8_t op2 = OP_RETRIEVE;
        send_all(fd, &op2, 1);
        send_string(fd, path);
    }

    uint32_t status = 0;
    if (recv_u32(fd, &status) != 0) {
        close(fd);
        print_warn("Lost connection to server mid-request.");
        if (!ensure_servers_up(context)) return;
        goto do_view;
    }

    printf("\n");
    print_separator();

    if (status == 0) {
        close(fd);
        char msg[256]; snprintf(msg, sizeof(msg), "File '%s' was NOT FOUND on any server.", path);
        print_err(msg);
        print_separator();
        return;
    }

    if (status == 1) {
        uint32_t len = 0;
        recv_u32(fd, &len);
        uint8_t *buf = (len > 0) ? (uint8_t *)malloc(len + 1) : NULL;
        if (len > 0 && buf) {
            recv_all(fd, buf, len);
            buf[len] = '\0';
        }
        close(fd);

        /* Print metadata */
        printf("  File: %s%s%s\n", g_use_color ? COL_BOLD : "", path, g_use_color ? COL_RESET : "");
        if (s1_found && s2_found && s1_size == s2_size) {
            printf("  Location: Server 1 and Server 2 " COL_GREEN "(identical)" COL_RESET "\n");
        } else if (s1_found && s2_found) {
            printf("  Location: Server 1 (%u bytes) and Server 2 (%u bytes) — copies differ\n",
                   s1_size, s2_size);
        } else if (s1_found) {
            printf("  Location: Server 1 only (%u bytes)\n", s1_size);
        } else {
            printf("  Location: Server 2 only (%u bytes)\n", s2_size);
        }
        printf("  Size:     %u byte%s\n\n", len, len == 1 ? "" : "s");

        if (g_use_color) printf(COL_CYAN "  Contents:\n" COL_RESET);
        else printf("  Contents:\n");
        print_separator();
        if (buf && len > 0) {
            /* Print with line numbers */
            int line_num = 1;
            char *p = (char *)buf;
            char *end = p + len;
            while (p < end) {
                char *nl = (char *)memchr(p, '\n', (size_t)(end - p));
                int line_len = nl ? (int)(nl - p) : (int)(end - p);
                if (g_use_color) printf(COL_YELLOW "%4d  " COL_RESET, line_num++);
                else printf("%4d  ", line_num++);
                fwrite(p, 1, (size_t)line_len, stdout);
                printf("\n");
                if (nl) p = nl + 1; else break;
            }
        } else {
            print_info("(empty file)");
        }
        print_separator();
        free(buf);
        return;
    }

    if (status == 2) {
        uint32_t len1 = 0, len2 = 0;
        recv_u32(fd, &len1);
        uint8_t *buf1 = (len1 > 0) ? (uint8_t *)malloc(len1 + 1) : NULL;
        if (len1 > 0 && buf1) { recv_all(fd, buf1, len1); buf1[len1] = '\0'; }
        recv_u32(fd, &len2);
        uint8_t *buf2 = (len2 > 0) ? (uint8_t *)malloc(len2 + 1) : NULL;
        if (len2 > 0 && buf2) { recv_all(fd, buf2, len2); buf2[len2] = '\0'; }
        close(fd);

        printf("  File: %s%s%s\n", g_use_color ? COL_BOLD : "", path, g_use_color ? COL_RESET : "");
        printf("  Location: Server 1 (%u bytes) and Server 2 (%u bytes)\n", len1, len2);
        print_warn("Both copies exist but contents DIFFER");
        printf("\n");

        if (g_use_color) printf(COL_CYAN "  Server 1 Contents (%u bytes):\n" COL_RESET, len1);
        else printf("  Server 1 Contents (%u bytes):\n", len1);
        print_separator();
        if (buf1 && len1 > 0) {
            int ln = 1;
            char *p = (char *)buf1, *end = p + len1;
            while (p < end) {
                char *nl = (char *)memchr(p, '\n', (size_t)(end - p));
                int ll = nl ? (int)(nl - p) : (int)(end - p);
                if (g_use_color) printf(COL_YELLOW "%4d  " COL_RESET, ln++);
                else printf("%4d  ", ln++);
                fwrite(p, 1, (size_t)ll, stdout);
                printf("\n");
                if (nl) p = nl + 1; else break;
            }
        }

        printf("\n");
        if (g_use_color) printf(COL_CYAN "  Server 2 Contents (%u bytes):\n" COL_RESET, len2);
        else printf("  Server 2 Contents (%u bytes):\n", len2);
        print_separator();
        if (buf2 && len2 > 0) {
            int ln = 1;
            char *p = (char *)buf2, *end = p + len2;
            while (p < end) {
                char *nl = (char *)memchr(p, '\n', (size_t)(end - p));
                int ll = nl ? (int)(nl - p) : (int)(end - p);
                if (g_use_color) printf(COL_YELLOW "%4d  " COL_RESET, ln++);
                else printf("%4d  ", ln++);
                fwrite(p, 1, (size_t)ll, stdout);
                printf("\n");
                if (nl) p = nl + 1; else break;
            }
        }
        print_separator();
        free(buf1); free(buf2);
        return;
    }

    close(fd);
    print_err("Unexpected response from server.");
}

/* ==========================================================
 * COMMAND: DELETE FILE
 * ========================================================== */

static void cmd_delete(void) {
    print_header("Delete File");
    printf("  Type " COL_YELLOW "!cancel" COL_RESET " at any prompt to return to main menu.\n\n");

    char path[MAX_PATH_LEN + 1];
    if (read_line("Enter file name: ", path, sizeof(path)) != 0) return;
    if (is_cancel(path)) { print_info("Cancelled."); return; }
    if (strlen(path) == 0) { print_err("File name cannot be empty."); return; }

    char context[MAX_PATH_LEN + 32];
    snprintf(context, sizeof(context), "delete '%s'", path);

    uint32_t s1_found = 0, s2_found = 0;
    uint8_t del_target = 99;

do_delete_check:
    if (!ensure_servers_up(context)) return;

    {
        int fd = connect_to_server1();
        if (fd < 0) {
            if (!ensure_servers_up(context)) return;
            goto do_delete_check;
        }

        uint8_t op = OP_DELETE;
        send_all(fd, &op, 1);
        send_string(fd, path);

        if (recv_u32(fd, &s1_found) != 0 || recv_u32(fd, &s2_found) != 0) {
            close(fd);
            print_warn("Lost connection to server mid-request.");
            if (!ensure_servers_up(context)) return;
            goto do_delete_check;
        }

        if (!s1_found && !s2_found) {
            close(fd);
            char msg[256]; snprintf(msg, sizeof(msg), "File '%s' was NOT FOUND on any server.", path);
            print_err(msg);
            return;
        }

        printf("\n");
        printf("  File '%s' found on:\n", path);
        if (s1_found) print_ok("Server 1");
        if (s2_found) print_ok("Server 2");
        printf("\n");

        /* Dynamic delete menu based on where it was found */
        int opt_count = 0;
        char opts[4];
        uint8_t opt_targets[4];

        if (s1_found && s2_found) {
            printf("  Delete from:\n");
            printf("    a. Both servers\n");
            printf("    b. Server 1 only\n");
            printf("    c. Server 2 only\n");
            printf("    r. Return to main menu\n\n");
            opts[0] = 'a'; opt_targets[0] = 0; opt_count++;
            opts[1] = 'b'; opt_targets[1] = 1; opt_count++;
            opts[2] = 'c'; opt_targets[2] = 2; opt_count++;
            opts[3] = 'r'; opt_targets[3] = 99; opt_count++;
        } else if (s1_found) {
            printf("  Delete from:\n");
            printf("    a. Server 1\n");
            printf("    r. Return to main menu\n\n");
            opts[0] = 'a'; opt_targets[0] = 1; opt_count++;
            opts[1] = 'r'; opt_targets[1] = 99; opt_count++;
        } else {
            printf("  Delete from:\n");
            printf("    a. Server 2\n");
            printf("    r. Return to main menu\n\n");
            opts[0] = 'a'; opt_targets[0] = 2; opt_count++;
            opts[1] = 'r'; opt_targets[1] = 99; opt_count++;
        }

        char choice = read_choice("Your choice: ");
        del_target = 99;
        for (int i = 0; i < opt_count; i++) {
            if (choice == opts[i]) { del_target = opt_targets[i]; break; }
        }

        if (del_target == 99) {
            close(fd);
            print_info("Delete cancelled.");
            return;
        }

        /* ---- Phase 2: send del_target and receive result ---- */
        /* If server dropped between the check and the delete, retry whole flow */
        if (send_all(fd, &del_target, 1) != 0) {
            close(fd);
            print_warn("Lost connection before delete could be sent — retrying...");
            if (!ensure_servers_up(context)) return;
            goto do_delete_check;
        }

        uint32_t s1_del = 0, s2_del = 0;
        if (recv_u32(fd, &s1_del) != 0 || recv_u32(fd, &s2_del) != 0) {
            close(fd);
            print_warn("Lost connection to server mid-request.");
            if (!ensure_servers_up(context)) return;
            goto do_delete_check;
        }
        close(fd);

        printf("\n");
        const char *del_labels[] = {"Not found", "Deleted", "Error", "Skipped"};
        if (del_target == 0 || del_target == 1) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Server 1: %s", del_labels[s1_del < 4 ? s1_del : 2]);
            if (s1_del == 1) print_ok(msg);
            else if (s1_del == 3) print_info(msg);
            else print_err(msg);
        }
        if (del_target == 0 || del_target == 2) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Server 2: %s", del_labels[s2_del < 4 ? s2_del : 2]);
            if (s2_del == 1) print_ok(msg);
            else if (s2_del == 3) print_info(msg);
            else print_err(msg);
        }
    }
}

/* ==========================================================
 * EXIT: Shutdown both servers
 * ========================================================== */

static void do_shutdown_and_exit(void) {
    print_header("Shutting Down");

    printf("  Sending shutdown signal to servers...\n\n");

    int fd = connect_to_server1();
    if (fd < 0) {
        print_warn("Could not connect to Server 1 — it may already be offline.");
    } else {
        uint8_t op = OP_SHUTDOWN;
        send_all(fd, &op, 1);
        uint32_t res = 0;
        recv_u32(fd, &res);
        close(fd);
        print_ok("Shutdown command sent to Server 1 (and relayed to Server 2).");
    }

    /* Poll until both servers are down */
    int attempts = 0;
    char spin_msg[128];
    while (attempts < 20) {
        int s1, s2;
        check_server_status(&s1, &s2);
        if (!s1 && !s2) {
            spinner_done();
            break;
        }
        snprintf(spin_msg, sizeof(spin_msg), "Waiting for servers to go offline...  %s%s",
                 s1 ? "[S1 still up] " : "", s2 ? "[S2 still up] " : "");
        spinner_tick(spin_msg);
        sleep(1);
        attempts++;
    }

    print_ok("Server 1 — OFFLINE");
    print_ok("Server 2 — OFFLINE");
    printf("\n");
    print_separator();
    if (g_use_color) printf(COL_BOLD COL_GREEN);
    printf("  Shutdown successful. Goodbye!\n");
    if (g_use_color) printf(COL_RESET);
    print_separator();
    printf("\n");
    exit(0);
}

/* ==========================================================
 * MAIN MENU
 * ========================================================== */

static void print_main_menu(void) {
    print_header("Distributed File System — Main Menu");
    printf("    a.  Download file\n");
    printf("    b.  File size\n");
    printf("    c.  Create file\n");
    printf("    d.  List files\n");
    printf("    e.  View file\n");
    printf("    f.  Delete file\n");
    printf("    g.  Exit\n");
    printf("\n");
}

static void main_menu_loop(void) {
    while (1) {
        print_main_menu();
        char choice = read_choice("Your choice: ");
        switch (choice) {
            case 'a': cmd_retrieve(); break;
            case 'b': cmd_size();     break;
            case 'c': cmd_create();   break;
            case 'd': cmd_list();     break;
            case 'e': cmd_view();     break;
            case 'f': cmd_delete();   break;
            case 'g': do_shutdown_and_exit(); break;
            default:
                print_err("Invalid choice. Please select a–g.");
                break;
        }
    }
}

/* ==========================================================
 * STARTUP FLOW
 * ========================================================== */

static void startup_flow(TermType ttype) {
    /* ---- Welcome ---- */
    printf("\n");
    print_separator();
    if (g_use_color) printf(COL_BOLD COL_BWHITE);
    printf("  Distributed File System Client\n");
    if (g_use_color) printf(COL_RESET);
    printf("  Server 1: %s:%d   Server 2: localhost:%d\n",
           g_server1_ip, g_server1_port, SERVER2_PORT);
    print_separator();
    printf("\n");

    /* ---- Ask user to start ---- */
ask_start:
    ;
    char ans = read_choice("Would you like to start the app? [y/n]: ");

    if (ans == 'n' || ans == 'N') {
        /* Confirm quit */
        char q = read_choice("Are you sure you want to quit? [y/n]: ");
        if (q == 'y' || q == 'Y') {
            printf("\n  Goodbye!\n\n");
            exit(0);
        }
        goto ask_start;
    }

    if (ans != 'y' && ans != 'Y') {
        print_err("Please enter y or n.");
        goto ask_start;
    }

    /* ---- Check server status ---- */
    printf("\n");
    print_info("Checking server availability...");
    printf("\n");

    int s1 = 0, s2 = 0;
    check_server_status(&s1, &s2);
    print_server_status(s1, s2);

    int need_s1 = !s1;
    int need_s2 = !s2;

    if (s1 && s2) {
        print_warn("Servers appear to already be running.");
        print_warn("The app may have closed unexpectedly last time.");
        print_ok("Server 1 is already running.");
        print_ok("Server 2 is already running.");
        printf("\n");
        /* Proceed directly to main menu */
        main_menu_loop();
        return;
    }

    if (s1 && !s2) {
        print_ok("Server 1 is already running.");
        print_warn("Server 2 is offline — launching Server 2 terminal...");
        launch_server_windows(2, ttype);
    } else if (!s1 && s2) {
        print_ok("Server 2 is already running.");
        print_warn("Server 1 is offline — launching Server 1 terminal...");
        launch_server_windows(1, ttype);
    } else {
        /* Both down — launch both (scripts are uniquely named, no delay needed) */
        print_info("Starting Server 2...");
        launch_server_windows(2, ttype);
        print_info("Starting Server 1...");
        launch_server_windows(1, ttype);
    }

    /* ---- Wait for servers to come online ---- */
    printf("\n");
    print_info("Starting up the app. Please wait...\n");

    int ready = wait_for_servers(need_s1, need_s2);

    if (!ready) {
        printf("\n");
        print_err("Servers did not come online within the timeout.");
        print_info("Please start the servers manually:");
        printf("    Server 1: ./bin/server1 5001 127.0.0.1 5002 ./server1_files\n");
        printf("    Server 2: ./bin/server2 5002 ./server2_files\n\n");
        exit(1);
    }

    /* ---- Final status ---- */
    printf("\n");
    check_server_status(&s1, &s2);
    if (s1) print_ok("Server 1 — ONLINE");
    else    print_err("Server 1 — OFFLINE (unexpected!)");
    if (s2) print_ok("Server 2 — ONLINE");
    else    print_err("Server 2 — OFFLINE (unexpected!)");

    printf("\n");
    if (g_use_color) printf(COL_BOLD COL_GREEN);
    printf("  App successfully started!\n");
    if (g_use_color) printf(COL_RESET);
    printf("\n");

    main_menu_loop();
}

/* ==========================================================
 * MAIN
 * ========================================================== */

int main(int argc, char **argv) {
#if PLATFORM_WINDOWS
    init_winsock();
    /* Try to enable VT100 colors on Windows 10+ */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    if (!SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        g_use_color = 0;
    }
#else
    /* Disable color if not a TTY (e.g. piped output) */
    if (!isatty(STDOUT_FILENO)) g_use_color = 0;
#endif

    if (argc >= 2) snprintf(g_server1_ip, sizeof(g_server1_ip), "%s", argv[1]);
    if (argc >= 3) g_server1_port = atoi(argv[2]);

    g_ttype = detect_terminal();

    ensure_client_files_dir();
    startup_flow(g_ttype);

#if PLATFORM_WINDOWS
    WSACleanup();
#endif
    return 0;
}
