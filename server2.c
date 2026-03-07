/*
 * server2.c
 * ---------
 * SERVER2: Replica file server.
 *
 * Responsibilities:
 *  - Listen on a TCP port.
 *  - Receive a pathname (string) from SERVER1.
 *  - Search file inside SERVER2 directory (base_dir + "/" + pathname).
 *  - If found -> send status=1 + file length + file bytes.
 *  - If not found -> send status=0.
 *
 * Usage:
 *   ./server2 <listen_port> <base_dir>
 *
 * Example:
 *   ./server2 5002 ./server2_files
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BACKLOG 16
#define MAX_PATH_LEN 4096
#define CHUNK 4096

/* ---- Helper: read exactly N bytes from socket ---- */
static int recv_all(int sock, void *buf, size_t n) {
    size_t got = 0;
    char *p = (char *)buf;
    while (got < n) {
        ssize_t r = recv(sock, p + got, n - got, 0);
        if (r == 0) return -1;              /* connection closed */
        if (r < 0) {
            if (errno == EINTR) continue;   /* retry if interrupted */
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

/* ---- Read entire file into memory buffer (binary-safe) ---- */
static int read_file_to_buf(const char *fullpath, uint8_t **out_buf, uint32_t *out_len) {
    int fd = open(fullpath, O_RDONLY);
    if (fd < 0) return -1;

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return -1;
    }
    if (st.st_size < 0 || st.st_size > (off_t)UINT32_MAX) { /* keep protocol uint32 */
        close(fd);
        errno = EFBIG;
        return -1;
    }

    uint32_t len = (uint32_t)st.st_size;
    uint8_t *buf = NULL;

    if (len > 0) {
        buf = (uint8_t *)malloc(len);
        if (!buf) {
            close(fd);
            return -1;
        }

        uint32_t read_total = 0;
        while (read_total < len) {
            ssize_t r = read(fd, buf + read_total, len - read_total);
            if (r < 0) {
                if (errno == EINTR) continue;
                free(buf);
                close(fd);
                return -1;
            }
            if (r == 0) break;
            read_total += (uint32_t)r;
        }

        if (read_total != len) {
            free(buf);
            close(fd);
            errno = EIO;
            return -1;
        }
    }

    close(fd);
    *out_buf = buf;
    *out_len = len;
    return 0;
}

/* ---- Protocol: receive pathname ----
 * Format:
 *   uint32_t path_len (network byte order)
 *   path bytes (NOT null-terminated)
 */
static int recv_pathname(int sock, char *out_path, size_t out_cap) {
    uint32_t nlen = 0;
    if (recv_all(sock, &nlen, sizeof(nlen)) != 0) return -1;

    uint32_t len = ntohl(nlen);
    if (len == 0 || len >= out_cap || len > MAX_PATH_LEN) return -1;

    if (recv_all(sock, out_path, len) != 0) return -1;
    out_path[len] = '\0'; /* make it a C string */
    return 0;
}

/* ---- Protocol: send reply ----
 * Reply format:
 *   uint32_t status (0=not found, 1=found)
 *   if status==1:
 *     uint32_t file_len
 *     file bytes
 */
static int send_reply_not_found(int sock) {
    uint32_t status = htonl(0);
    return send_all(sock, &status, sizeof(status));
}

static int send_reply_found(int sock, const uint8_t *buf, uint32_t len) {
    uint32_t status = htonl(1);
    uint32_t nlen = htonl(len);

    if (send_all(sock, &status, sizeof(status)) != 0) return -1;
    if (send_all(sock, &nlen, sizeof(nlen)) != 0) return -1;
    if (len > 0 && send_all(sock, buf, len) != 0) return -1;
    return 0;
}

/* ---- Safer join: base_dir + "/" + relative_path ---- */
static int build_fullpath(char *dst, size_t cap, const char *base_dir, const char *path) {
    /* Basic safety: block absolute paths and parent traversal */
    if (path[0] == '/' || strstr(path, "..") != NULL) {
        return -1;
    }
    int n = snprintf(dst, cap, "%s/%s", base_dir, path);
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <listen_port> <base_dir>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    const char *base_dir = argv[2];

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, BACKLOG) != 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    printf("[SERVER2] Listening on port %d, base_dir=%s\n", port, base_dir);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int cfd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        char path[MAX_PATH_LEN + 1];
        if (recv_pathname(cfd, path, sizeof(path)) != 0) {
            fprintf(stderr, "[SERVER2] Failed to receive pathname\n");
            close(cfd);
            continue;
        }

        printf("[SERVER2] Request for '%s'\n", path);

        char fullpath[MAX_PATH_LEN + 1];
        if (build_fullpath(fullpath, sizeof(fullpath), base_dir, path) != 0) {
            fprintf(stderr, "[SERVER2] Invalid path '%s'\n", path);
            send_reply_not_found(cfd);
            close(cfd);
            continue;
        }

        uint8_t *buf = NULL;
        uint32_t len = 0;
        if (read_file_to_buf(fullpath, &buf, &len) != 0) {
            printf("[SERVER2] Not found or error opening '%s' (%s)\n", fullpath, strerror(errno));
            send_reply_not_found(cfd);
        } else {
            printf("[SERVER2] Found '%s' (%u bytes)\n", fullpath, len);
            send_reply_found(cfd, buf, len);
        }

        free(buf);
        close(cfd);
    }

    close(listen_fd);
    return 0;
}