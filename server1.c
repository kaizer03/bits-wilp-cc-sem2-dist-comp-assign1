/*
 * server1.c
 * ---------
 * SERVER1: Primary file server.
 *
 * Responsibilities (as per assignment):
 *  - Listen for CLIENT requests (pathname).
 *  - Check local folder for file.
 *  - Forward same pathname to SERVER2 (replica).
 *  - If file exists on one server, return it to client via SERVER1.
 *  - If both exist:
 *      - If identical => send one file to client
 *      - If different => send both files to client
 *  - If none => send NOT FOUND to client
 *
 * Usage:
 *   ./server1 <listen_port> <server2_ip> <server2_port> <base_dir_server1>
 *
 * Example:
 *   ./server1 5001 127.0.0.1 5002 ./server1_files
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

/* ---- Helper: send exactly N bytes ---- */
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
    if (fstat(fd, &st) != 0) {
        close(fd);
        return -1;
    }
    if (st.st_size < 0 || st.st_size > (off_t)UINT32_MAX) {
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

/* ---- Build full path: base_dir + "/" + path ---- */
static int build_fullpath(char *dst, size_t cap, const char *base_dir, const char *path) {
    if (path[0] == '/' || strstr(path, "..") != NULL) return -1;
    int n = snprintf(dst, cap, "%s/%s", base_dir, path);
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

/* ---- Receive pathname from CLIENT ----
 * Format:
 *   uint32_t path_len
 *   path bytes
 */
static int recv_pathname(int sock, char *out_path, size_t out_cap) {
    uint32_t nlen = 0;
    if (recv_all(sock, &nlen, sizeof(nlen)) != 0) return -1;

    uint32_t len = ntohl(nlen);
    if (len == 0 || len >= out_cap || len > MAX_PATH_LEN) return -1;

    if (recv_all(sock, out_path, len) != 0) return -1;
    out_path[len] = '\0';
    return 0;
}

/* ---- Connect to SERVER2 ---- */
static int connect_to_server2(const char *ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) {
        close(fd);
        errno = EINVAL;
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

/* ---- Send pathname to SERVER2 ---- */
static int send_pathname(int sock, const char *path) {
    uint32_t len = (uint32_t)strlen(path);
    uint32_t nlen = htonl(len);

    if (send_all(sock, &nlen, sizeof(nlen)) != 0) return -1;
    if (send_all(sock, path, len) != 0) return -1;
    return 0;
}

/* ---- Receive reply from SERVER2 ----
 * Reply:
 *   uint32_t status (0 or 1)
 *   if status==1:
 *     uint32_t file_len
 *     file bytes
 */
static int recv_server2_reply(int sock, uint8_t **out_buf, uint32_t *out_len, int *out_found) {
    *out_buf = NULL;
    *out_len = 0;
    *out_found = 0;

    uint32_t nstatus = 0;
    if (recv_all(sock, &nstatus, sizeof(nstatus)) != 0) return -1;

    uint32_t status = ntohl(nstatus);
    if (status == 0) {
        *out_found = 0;
        return 0;
    }
    if (status != 1) return -1;

    uint32_t nlen = 0;
    if (recv_all(sock, &nlen, sizeof(nlen)) != 0) return -1;

    uint32_t len = ntohl(nlen);
    uint8_t *buf = NULL;

    if (len > 0) {
        buf = (uint8_t *)malloc(len);
        if (!buf) return -1;
        if (recv_all(sock, buf, len) != 0) {
            free(buf);
            return -1;
        }
    }

    *out_found = 1;
    *out_buf = buf;
    *out_len = len;
    return 0;
}

/* ---- Send reply to CLIENT ----
 * Reply:
 *   uint32_t status
 *     0 = not found
 *     1 = sending ONE file
 *     2 = sending TWO files (different)
 *   Then for each file:
 *     uint32_t file_len
 *     file bytes
 */
static int send_client_not_found(int sock) {
    uint32_t st = htonl(0);
    return send_all(sock, &st, sizeof(st));
}

static int send_client_one_file(int sock, const uint8_t *buf, uint32_t len) {
    uint32_t st = htonl(1);
    uint32_t nlen = htonl(len);

    if (send_all(sock, &st, sizeof(st)) != 0) return -1;
    if (send_all(sock, &nlen, sizeof(nlen)) != 0) return -1;
    if (len > 0 && send_all(sock, buf, len) != 0) return -1;
    return 0;
}

static int send_client_two_files(int sock,
                                 const uint8_t *buf1, uint32_t len1,
                                 const uint8_t *buf2, uint32_t len2) {
    uint32_t st = htonl(2);
    uint32_t nlen1 = htonl(len1);
    uint32_t nlen2 = htonl(len2);

    if (send_all(sock, &st, sizeof(st)) != 0) return -1;

    if (send_all(sock, &nlen1, sizeof(nlen1)) != 0) return -1;
    if (len1 > 0 && send_all(sock, buf1, len1) != 0) return -1;

    if (send_all(sock, &nlen2, sizeof(nlen2)) != 0) return -1;
    if (len2 > 0 && send_all(sock, buf2, len2) != 0) return -1;

    return 0;
}

/* ---- Compare two memory buffers ---- */
static int buffers_equal(const uint8_t *a, uint32_t alen, const uint8_t *b, uint32_t blen) {
    if (alen != blen) return 0;
    if (alen == 0) return 1; /* both empty */
    return (memcmp(a, b, alen) == 0) ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <listen_port> <server2_ip> <server2_port> <base_dir_server1>\n", argv[0]);
        return 1;
    }

    int listen_port = atoi(argv[1]);
    const char *server2_ip = argv[2];
    int server2_port = atoi(argv[3]);
    const char *base_dir = argv[4];

    /* --- Create listening socket for CLIENT --- */
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
    addr.sin_port = htons((uint16_t)listen_port);
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

    printf("[SERVER1] Listening on port %d, base_dir=%s, server2=%s:%d\n",
           listen_port, base_dir, server2_ip, server2_port);

    while (1) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(listen_fd, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        char path[MAX_PATH_LEN + 1];
        if (recv_pathname(cfd, path, sizeof(path)) != 0) {
            fprintf(stderr, "[SERVER1] Failed to receive pathname from client\n");
            close(cfd);
            continue;
        }

        printf("\n[SERVER1] Client requested '%s'\n", path);

        /* --- 1) Check file on SERVER1 local folder --- */
        uint8_t *buf1 = NULL;
        uint32_t len1 = 0;
        int found1 = 0;

        char full1[MAX_PATH_LEN + 1];
        if (build_fullpath(full1, sizeof(full1), base_dir, path) == 0) {
            if (read_file_to_buf(full1, &buf1, &len1) == 0) {
                found1 = 1;
                printf("[SERVER1] Found locally: %s (%u bytes)\n", full1, len1);
            } else {
                printf("[SERVER1] Not found locally: %s (%s)\n", full1, strerror(errno));
            }
        } else {
            printf("[SERVER1] Invalid client path (blocked): %s\n", path);
        }

        /* --- 2) Ask SERVER2 for the file --- */
        uint8_t *buf2 = NULL;
        uint32_t len2 = 0;
        int found2 = 0;

        int s2 = connect_to_server2(server2_ip, server2_port);
        if (s2 < 0) {
            printf("[SERVER1] WARNING: Could not connect to SERVER2 (%s)\n", strerror(errno));
            /* If SERVER2 is down, still serve local if found */
        } else {
            if (send_pathname(s2, path) != 0) {
                printf("[SERVER1] WARNING: Failed to send request to SERVER2\n");
            } else {
                if (recv_server2_reply(s2, &buf2, &len2, &found2) != 0) {
                    printf("[SERVER1] WARNING: Failed to receive reply from SERVER2\n");
                    found2 = 0;
                } else {
                    if (found2) printf("[SERVER1] SERVER2 has file (%u bytes)\n", len2);
                    else printf("[SERVER1] SERVER2 does NOT have file\n");
                }
            }
            close(s2);
        }

        /* --- 3) Decide what to send to CLIENT (per assignment) --- */
        if (!found1 && !found2) {
            printf("[SERVER1] File not available on any server. Sending NOT FOUND to client.\n");
            send_client_not_found(cfd);
        } else if (found1 && !found2) {
            printf("[SERVER1] Only SERVER1 has file. Sending one file to client.\n");
            send_client_one_file(cfd, buf1, len1);
        } else if (!found1 && found2) {
            printf("[SERVER1] Only SERVER2 has file. Sending one file to client (via SERVER1).\n");
            send_client_one_file(cfd, buf2, len2);
        } else {
            /* found on both servers */
            if (buffers_equal(buf1, len1, buf2, len2)) {
                printf("[SERVER1] Both copies are IDENTICAL. Sending one file to client.\n");
                send_client_one_file(cfd, buf1, len1);
            } else {
                printf("[SERVER1] Copies are DIFFERENT. Sending BOTH files to client.\n");
                send_client_two_files(cfd, buf1, len1, buf2, len2);
            }
        }

        free(buf1);
        free(buf2);
        close(cfd);
    }

    close(listen_fd);
    return 0;
}