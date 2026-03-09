/*
 * server2.c
 * ---------
 * SERVER2: Replica file server.
 *
 * Extended Protocol (opcode-based):
 *   Every request from SERVER1 starts with:
 *     uint8_t  opcode
 *   Followed by opcode-specific payload.
 *
 * Opcodes:
 *   0x01  PING        — no payload; reply uint32_t=1
 *   0x02  RETRIEVE    — [uint32_t path_len][path]; reply [uint32_t status][uint32_t len][bytes]
 *   0x03  FILE_SIZE   — [uint32_t path_len][path]; reply [uint32_t status][uint32_t size]
 *   0x04  CREATE      — [uint32_t path_len][path][uint32_t content_len][content]
 *                       reply [uint32_t status]  1=ok 0=fail
 *   0x05  LIST        — no extra payload; reply [uint32_t count]([uint32_t name_len][name][uint32_t size])...
 *   0x06  DELETE      — [uint32_t path_len][path]; reply [uint32_t status] 1=deleted 0=notfound 2=error
 *   0x07  SHUTDOWN    — no payload; server exits after replying uint32_t=0
 *
 * Usage:
 *   ./bin/server2 <listen_port> <base_dir>
 *
 * Example:
 *   ./bin/server2 5002 ./server2_files
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <dirent.h>
#include <signal.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BACKLOG      16
#define MAX_PATH_LEN 4096

/* ---- Opcodes ---- */
#define OP_PING     0x01
#define OP_RETRIEVE 0x02
#define OP_SIZE     0x03
#define OP_CREATE   0x04
#define OP_LIST     0x05
#define OP_DELETE   0x06
#define OP_SHUTDOWN 0x07

/* global flag for graceful shutdown */
static volatile int g_shutdown = 0;

/* ---- Helper: read exactly N bytes from socket ---- */
static int recv_all(int sock, void *buf, size_t n) {
    size_t got = 0;
    char *p = (char *)buf;
    while (got < n) {
        ssize_t r = recv(sock, p + got, n - got, 0);
        if (r == 0) return -1;
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += (size_t)r;
    }
    return 0;
}

/* ---- Helper: send exactly N bytes to socket ---- */
static int send_all(int sock, const void *buf, size_t n) {
    size_t sent = 0;
    const char *p = (const char *)buf;
    while (sent < n) {
        ssize_t s = send(sock, p + sent, n - sent, 0);
        if (s < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)s;
    }
    return 0;
}

/* ---- Read entire file into memory buffer ---- */
static int read_file_to_buf(const char *fullpath, uint8_t **out_buf, uint32_t *out_len) {
    int fd = open(fullpath, O_RDONLY);
    if (fd < 0) return -1;

    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return -1; }
    if (st.st_size < 0 || st.st_size > (off_t)UINT32_MAX) {
        close(fd); errno = EFBIG; return -1;
    }

    uint32_t len = (uint32_t)st.st_size;
    uint8_t *buf = NULL;

    if (len > 0) {
        buf = (uint8_t *)malloc(len);
        if (!buf) { close(fd); return -1; }

        uint32_t read_total = 0;
        while (read_total < len) {
            ssize_t r = read(fd, buf + read_total, len - read_total);
            if (r < 0) { if (errno == EINTR) continue; free(buf); close(fd); return -1; }
            if (r == 0) break;
            read_total += (uint32_t)r;
        }
        if (read_total != len) { free(buf); close(fd); errno = EIO; return -1; }
    }

    close(fd);
    *out_buf = buf;
    *out_len = len;
    return 0;
}

/* ---- Build full path: base_dir + "/" + path, blocking traversal ---- */
static int build_fullpath(char *dst, size_t cap, const char *base_dir, const char *path) {
    if (path[0] == '/' || strstr(path, "..") != NULL) return -1;
    int n = snprintf(dst, cap, "%s/%s", base_dir, path);
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

/* ---- Receive a length-prefixed string from socket ---- */
static int recv_string(int sock, char *out, size_t cap) {
    uint32_t nlen = 0;
    if (recv_all(sock, &nlen, sizeof(nlen)) != 0) return -1;
    uint32_t len = ntohl(nlen);
    if (len == 0 || len >= cap || len > MAX_PATH_LEN) return -1;
    if (recv_all(sock, out, len) != 0) return -1;
    out[len] = '\0';
    return 0;
}

/* ---- Send a uint32_t status ---- */
static int send_u32(int sock, uint32_t val) {
    uint32_t n = htonl(val);
    return send_all(sock, &n, sizeof(n));
}

/* ---- Handle PING ---- */
static void handle_ping(int cfd) {
    printf("[SERVER2] Status check received from SERVER1 — responding ONLINE\n");
    send_u32(cfd, 1);
}

/* ---- Handle RETRIEVE ---- */
static void handle_retrieve(int cfd, const char *base_dir) {
    char path[MAX_PATH_LEN + 1];
    if (recv_string(cfd, path, sizeof(path)) != 0) {
        fprintf(stderr, "[SERVER2] DOWNLOAD: failed to receive filename from SERVER1\n");
        return;
    }

    printf("[SERVER2] DOWNLOAD request received from SERVER1 — file: '%s'\n", path);

    char fullpath[MAX_PATH_LEN + 1];
    if (build_fullpath(fullpath, sizeof(fullpath), base_dir, path) != 0) {
        fprintf(stderr, "[SERVER2] DOWNLOAD: invalid path '%s' — rejecting\n", path);
        send_u32(cfd, 0);
        return;
    }

    uint8_t *buf = NULL;
    uint32_t len = 0;
    if (read_file_to_buf(fullpath, &buf, &len) != 0) {
        printf("[SERVER2] DOWNLOAD: '%s' not found in local storage — responding not found to SERVER1\n", path);
        send_u32(cfd, 0);
        return;
    }

    printf("[SERVER2] DOWNLOAD: '%s' found (%u bytes) — sending to SERVER1\n", path, len);
    send_u32(cfd, 1);
    send_u32(cfd, len);
    if (len > 0) send_all(cfd, buf, len);
    free(buf);
}

/* ---- Handle FILE_SIZE ---- */
static void handle_size(int cfd, const char *base_dir) {
    char path[MAX_PATH_LEN + 1];
    if (recv_string(cfd, path, sizeof(path)) != 0) {
        fprintf(stderr, "[SERVER2] FILE SIZE: failed to receive filename from SERVER1\n");
        return;
    }

    printf("[SERVER2] FILE SIZE query received from SERVER1 — file: '%s'\n", path);

    char fullpath[MAX_PATH_LEN + 1];
    if (build_fullpath(fullpath, sizeof(fullpath), base_dir, path) != 0) {
        fprintf(stderr, "[SERVER2] FILE SIZE: invalid path '%s' — responding not found to SERVER1\n", path);
        send_u32(cfd, 0);
        send_u32(cfd, 0);
        return;
    }

    struct stat st;
    if (stat(fullpath, &st) != 0) {
        printf("[SERVER2] FILE SIZE: '%s' not found in local storage — responding not found to SERVER1\n", path);
        send_u32(cfd, 0);
        send_u32(cfd, 0);
        return;
    }

    uint32_t sz = (uint32_t)st.st_size;
    printf("[SERVER2] FILE SIZE: '%s' found — %u bytes — sending to SERVER1\n", path, sz);
    send_u32(cfd, 1);
    send_u32(cfd, sz);
}

/* ---- Handle CREATE ---- */
static void handle_create(int cfd, const char *base_dir) {
    char path[MAX_PATH_LEN + 1];
    if (recv_string(cfd, path, sizeof(path)) != 0) {
        fprintf(stderr, "[SERVER2] CREATE: failed to receive path\n");
        return;
    }

    /* receive content length */
    uint32_t nclen = 0;
    if (recv_all(cfd, &nclen, sizeof(nclen)) != 0) {
        fprintf(stderr, "[SERVER2] CREATE: failed to receive content len\n");
        return;
    }
    uint32_t clen = ntohl(nclen);

    uint8_t *content = NULL;
    if (clen > 0) {
        content = (uint8_t *)malloc(clen);
        if (!content) { send_u32(cfd, 0); return; }
        if (recv_all(cfd, content, clen) != 0) {
            free(content);
            fprintf(stderr, "[SERVER2] CREATE: failed to receive content\n");
            return;
        }
    }

    printf("[SERVER2] CREATE request received from SERVER1 — file: '%s'  content: %u bytes\n", path, clen);

    char fullpath[MAX_PATH_LEN + 1];
    if (build_fullpath(fullpath, sizeof(fullpath), base_dir, path) != 0) {
        fprintf(stderr, "[SERVER2] CREATE: invalid path '%s' — rejecting\n", path);
        free(content);
        send_u32(cfd, 0);
        return;
    }

    int fd = open(fullpath, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        fprintf(stderr, "[SERVER2] CREATE: failed to create '%s' — %s — notifying SERVER1\n",
                path, strerror(errno));
        free(content);
        send_u32(cfd, 0);
        return;
    }

    if (clen > 0) {
        uint32_t written = 0;
        while (written < clen) {
            ssize_t w = write(fd, content + written, clen - written);
            if (w < 0) { if (errno == EINTR) continue; close(fd); free(content); send_u32(cfd, 0); return; }
            written += (uint32_t)w;
        }
    }

    close(fd);
    free(content);
    printf("[SERVER2] CREATE: '%s' written successfully to local storage — notifying SERVER1\n", path);
    send_u32(cfd, 1);
}

/* ---- Handle LIST ---- */
static void handle_list(int cfd, const char *base_dir) {
    printf("[SERVER2] LIST FILES request received from SERVER1\n");

    DIR *dp = opendir(base_dir);
    if (!dp) {
        fprintf(stderr, "[SERVER2] LIST: cannot open local directory '%s' — %s — sending empty list to SERVER1\n",
                base_dir, strerror(errno));
        send_u32(cfd, 0);
        return;
    }

    /* First pass: count regular files */
    uint32_t count = 0;
    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char full[MAX_PATH_LEN + 1];
        snprintf(full, sizeof(full), "%s/%s", base_dir, ent->d_name);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISREG(st.st_mode)) count++;
    }

    printf("[SERVER2] LIST: found %u file(s) in local storage — sending listing to SERVER1\n", count);
    send_u32(cfd, count);

    /* Second pass: send file names and sizes */
    rewinddir(dp);
    while ((ent = readdir(dp)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char full[MAX_PATH_LEN + 1];
        snprintf(full, sizeof(full), "%s/%s", base_dir, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        uint32_t nlen = (uint32_t)strlen(ent->d_name);
        uint32_t nnlen = htonl(nlen);
        send_all(cfd, &nnlen, sizeof(nnlen));
        send_all(cfd, ent->d_name, nlen);
        send_u32(cfd, (uint32_t)st.st_size);
        printf("[SERVER2] LIST:   '%s'  %u bytes\n", ent->d_name, (uint32_t)st.st_size);
    }

    closedir(dp);
    printf("[SERVER2] LIST: complete — listing sent to SERVER1\n");
}

/* ---- Handle DELETE ---- */
static void handle_delete(int cfd, const char *base_dir) {
    char path[MAX_PATH_LEN + 1];
    if (recv_string(cfd, path, sizeof(path)) != 0) {
        fprintf(stderr, "[SERVER2] DELETE: failed to receive filename from SERVER1\n");
        return;
    }

    printf("[SERVER2] DELETE request received from SERVER1 — file: '%s'\n", path);

    char fullpath[MAX_PATH_LEN + 1];
    if (build_fullpath(fullpath, sizeof(fullpath), base_dir, path) != 0) {
        fprintf(stderr, "[SERVER2] DELETE: invalid path '%s' — notifying SERVER1\n", path);
        send_u32(cfd, 2);
        return;
    }

    if (access(fullpath, F_OK) != 0) {
        printf("[SERVER2] DELETE: '%s' not found in local storage — notifying SERVER1\n", path);
        send_u32(cfd, 0);
        return;
    }

    if (unlink(fullpath) != 0) {
        fprintf(stderr, "[SERVER2] DELETE: failed to delete '%s' — %s — notifying SERVER1\n",
                path, strerror(errno));
        send_u32(cfd, 2);
        return;
    }

    printf("[SERVER2] DELETE: '%s' successfully removed from local storage — notifying SERVER1\n", path);
    send_u32(cfd, 1);
}

/* ---- Handle SHUTDOWN ---- */
static void handle_shutdown(int cfd) {
    printf("[SERVER2] SHUTDOWN command received from SERVER1 — acknowledging and going offline\n");
    send_u32(cfd, 0);
    g_shutdown = 1;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <listen_port> <base_dir>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    const char *base_dir = argv[2];

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind"); close(listen_fd); return 1;
    }
    if (listen(listen_fd, BACKLOG) != 0) {
        perror("listen"); close(listen_fd); return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    printf("[SERVER2] Listening on port %d, base_dir=%s\n", port, base_dir);
    fflush(stdout);

    while (!g_shutdown) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(listen_fd, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            if (g_shutdown) break;
            perror("accept");
            break;
        }

        /* read opcode */
        uint8_t opcode = 0;
        if (recv_all(cfd, &opcode, 1) != 0) {
            fprintf(stderr, "[SERVER2] Failed to read opcode\n");
            close(cfd);
            continue;
        }

        switch (opcode) {
            case OP_PING:     handle_ping(cfd);                break;
            case OP_RETRIEVE: handle_retrieve(cfd, base_dir);  break;
            case OP_SIZE:     handle_size(cfd, base_dir);      break;
            case OP_CREATE:   handle_create(cfd, base_dir);    break;
            case OP_LIST:     handle_list(cfd, base_dir);      break;
            case OP_DELETE:   handle_delete(cfd, base_dir);    break;
            case OP_SHUTDOWN: handle_shutdown(cfd);            break;
            default:
                fprintf(stderr, "[SERVER2] Unknown opcode: 0x%02x\n", opcode);
                break;
        }

        close(cfd);
    }

    close(listen_fd);
    printf("[SERVER2] Shutdown complete.\n");
    return 0;
}
