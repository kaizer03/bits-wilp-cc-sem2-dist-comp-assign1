/*
 * server1.c
 * ---------
 * SERVER1: Primary file server and orchestrator.
 *
 * Extended Protocol (opcode-based):
 *   Every request from CLIENT starts with:
 *     uint8_t  opcode
 *   Followed by opcode-specific payload.
 *
 * Opcodes (CLIENT → SERVER1):
 *   0x01  PING        — reply: [uint32_t s1_status][uint32_t s2_status]
 *   0x02  RETRIEVE    — [uint32_t path_len][path]
 *                       reply: [uint32_t status][files per original protocol]
 *   0x03  FILE_SIZE   — [uint32_t path_len][path]
 *                       reply: [uint32_t s1_found][uint32_t s1_size]
 *                              [uint32_t s2_found][uint32_t s2_size]
 *   0x04  CREATE      — [uint8_t target: 0=both,1=s1,2=s2]
 *                       [uint32_t path_len][path][uint32_t content_len][content]
 *                       reply: [uint32_t s1_status][uint32_t s2_status]
 *   0x05  LIST        — [uint8_t target: 0=both,1=s1,2=s2]
 *                       reply: [uint32_t s1_count]([name_len][name][size])...
 *                              [uint32_t s2_count]([name_len][name][size])...
 *   0x06  DELETE      — [uint32_t path_len][path]
 *                       reply: [uint32_t s1_status][uint32_t s2_status]
 *                              (0=notfound, 1=deleted, 2=error, 3=skipped/not_applicable)
 *   0x07  SHUTDOWN    — shuts down SERVER1; also forwards shutdown to SERVER2
 *                       reply: [uint32_t 0]
 *
 * Usage:
 *   ./bin/server1 <listen_port> <server2_ip> <server2_port> <base_dir_server1>
 *
 * Example:
 *   ./bin/server1 5001 127.0.0.1 5002 ./server1_files
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

/* ---- Opcodes (shared with client and server2) ---- */
#define OP_PING     0x01
#define OP_RETRIEVE 0x02
#define OP_SIZE     0x03
#define OP_CREATE   0x04
#define OP_LIST     0x05
#define OP_DELETE   0x06
#define OP_SHUTDOWN 0x07

/* CREATE target values */
#define CREATE_BOTH 0
#define CREATE_S1   1
#define CREATE_S2   2

/* LIST target values */
#define LIST_BOTH 0
#define LIST_S1   1
#define LIST_S2   2

static volatile int g_shutdown = 0;

/* global server2 connection info (set in main) */
static const char *g_s2_ip   = NULL;
static int         g_s2_port = 0;

/* ---- Helper: read exactly N bytes from socket ---- */
static int recv_all(int sock, void *buf, size_t n) {
    size_t got = 0;
    char *p = (char *)buf;
    while (got < n) {
        ssize_t r = recv(sock, p + got, n - got, 0);
        if (r == 0) return -1;
        if (r < 0) { if (errno == EINTR) continue; return -1; }
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
        if (s < 0) { if (errno == EINTR) continue; return -1; }
        sent += (size_t)s;
    }
    return 0;
}

/* ---- Send a uint32_t ---- */
static int send_u32(int sock, uint32_t val) {
    uint32_t n = htonl(val);
    return send_all(sock, &n, sizeof(n));
}

/* ---- Receive a uint32_t ---- */
static int recv_u32(int sock, uint32_t *out) {
    uint32_t n = 0;
    if (recv_all(sock, &n, sizeof(n)) != 0) return -1;
    *out = ntohl(n);
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
        uint32_t tot = 0;
        while (tot < len) {
            ssize_t r = read(fd, buf + tot, len - tot);
            if (r < 0) { if (errno == EINTR) continue; free(buf); close(fd); return -1; }
            if (r == 0) break;
            tot += (uint32_t)r;
        }
        if (tot != len) { free(buf); close(fd); errno = EIO; return -1; }
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

/* ---- Compare two memory buffers ---- */
static int buffers_equal(const uint8_t *a, uint32_t alen, const uint8_t *b, uint32_t blen) {
    if (alen != blen) return 0;
    if (alen == 0) return 1;
    return (memcmp(a, b, alen) == 0) ? 1 : 0;
}

/* ---- Receive a length-prefixed string from socket ---- */
static int recv_string(int sock, char *out, size_t cap) {
    uint32_t len = 0;
    if (recv_u32(sock, &len) != 0) return -1;
    if (len == 0 || len >= cap || len > MAX_PATH_LEN) return -1;
    if (recv_all(sock, out, len) != 0) return -1;
    out[len] = '\0';
    return 0;
}

/* ---- Send a length-prefixed string to socket ---- */
static int send_string(int sock, const char *str) {
    uint32_t len = (uint32_t)strlen(str);
    if (send_u32(sock, len) != 0) return -1;
    return send_all(sock, str, len);
}

/* ---- Connect to SERVER2, returns fd or -1 ---- */
static int connect_to_server2(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)g_s2_port);

    if (inet_pton(AF_INET, g_s2_ip, &a.sin_addr) != 1) {
        close(fd); errno = EINVAL; return -1;
    }
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        close(fd); return -1;
    }
    return fd;
}

/* ---- Send opcode to SERVER2 and get connection fd, or -1 ---- */
static int s2_open(uint8_t opcode) {
    int fd = connect_to_server2();
    if (fd < 0) return -1;
    if (send_all(fd, &opcode, 1) != 0) { close(fd); return -1; }
    return fd;
}

/* =================================================================
 * HANDLER: PING
 * Check our own liveness + forward ping to SERVER2
 * Reply to client: [uint32_t s1_up=1][uint32_t s2_up]
 * ================================================================= */
static void handle_ping(int cfd) {
    printf("[SERVER1] Status check request received from client\n");
    printf("[SERVER1] Forwarding status check to SERVER2...\n");

    uint32_t s2_up = 0;
    int s2 = s2_open(OP_PING);
    if (s2 >= 0) {
        uint32_t res = 0;
        if (recv_u32(s2, &res) == 0 && res == 1) s2_up = 1;
        close(s2);
        printf("[SERVER1] SERVER2 responded: %s\n", s2_up ? "ONLINE" : "OFFLINE");
    } else {
        printf("[SERVER1] SERVER2 did not respond — marking as OFFLINE\n");
    }

    printf("[SERVER1] Status summary → SERVER1: ONLINE  SERVER2: %s\n",
           s2_up ? "ONLINE" : "OFFLINE");
    printf("[SERVER1] Sending combined status to client\n");

    send_u32(cfd, 1);
    send_u32(cfd, s2_up);
}

/* =================================================================
 * HANDLER: RETRIEVE (original assignment logic)
 * Reply to client: [uint32_t status][files...]
 *   status=0 not found
 *   status=1 one file: [uint32_t len][bytes]
 *   status=2 two files: [uint32_t len1][bytes1][uint32_t len2][bytes2]
 * ================================================================= */
static void handle_retrieve(int cfd, const char *base_dir) {
    char path[MAX_PATH_LEN + 1];
    if (recv_string(cfd, path, sizeof(path)) != 0) {
        fprintf(stderr, "[SERVER1] DOWNLOAD: failed to receive filename from client\n");
        return;
    }
    printf("[SERVER1] DOWNLOAD request received from client — file: '%s'\n", path);

    /* Check local */
    uint8_t *buf1 = NULL; uint32_t len1 = 0; int found1 = 0;
    char full1[MAX_PATH_LEN + 1];
    if (build_fullpath(full1, sizeof(full1), base_dir, path) == 0) {
        if (read_file_to_buf(full1, &buf1, &len1) == 0) {
            found1 = 1;
            printf("[SERVER1] DOWNLOAD: '%s' found locally (%u bytes)\n", path, len1);
        } else {
            printf("[SERVER1] DOWNLOAD: '%s' not found locally\n", path);
        }
    }

    /* Forward request to SERVER2 */
    printf("[SERVER1] DOWNLOAD: forwarding request for '%s' to SERVER2...\n", path);
    uint8_t *buf2 = NULL; uint32_t len2 = 0; int found2 = 0;
    int s2 = s2_open(OP_RETRIEVE);
    if (s2 >= 0) {
        if (send_string(s2, path) == 0) {
            uint32_t st2 = 0;
            if (recv_u32(s2, &st2) == 0 && st2 == 1) {
                if (recv_u32(s2, &len2) == 0) {
                    if (len2 > 0) {
                        buf2 = (uint8_t *)malloc(len2);
                        if (buf2 && recv_all(s2, buf2, len2) == 0) found2 = 1;
                        else { free(buf2); buf2 = NULL; }
                    } else {
                        found2 = 1;
                    }
                }
            }
        }
        close(s2);
        if (found2)
            printf("[SERVER1] DOWNLOAD: SERVER2 has '%s' (%u bytes)\n", path, len2);
        else
            printf("[SERVER1] DOWNLOAD: SERVER2 does not have '%s'\n", path);
    } else {
        printf("[SERVER1] DOWNLOAD: SERVER2 unreachable — proceeding with SERVER1 data only\n");
    }

    /* Decision and response */
    if (!found1 && !found2) {
        printf("[SERVER1] DOWNLOAD: '%s' not found on either server — notifying client\n", path);
        send_u32(cfd, 0);
    } else if (found1 && !found2) {
        printf("[SERVER1] DOWNLOAD: '%s' exists on SERVER1 only — sending SERVER1 copy to client\n", path);
        send_u32(cfd, 1); send_u32(cfd, len1);
        if (len1 > 0) send_all(cfd, buf1, len1);
    } else if (!found1 && found2) {
        printf("[SERVER1] DOWNLOAD: '%s' exists on SERVER2 only — relaying SERVER2 copy to client via SERVER1\n", path);
        send_u32(cfd, 1); send_u32(cfd, len2);
        if (len2 > 0) send_all(cfd, buf2, len2);
    } else {
        if (buffers_equal(buf1, len1, buf2, len2)) {
            printf("[SERVER1] DOWNLOAD: '%s' exists on both servers — copies are IDENTICAL — sending single copy to client\n", path);
            send_u32(cfd, 1); send_u32(cfd, len1);
            if (len1 > 0) send_all(cfd, buf1, len1);
        } else {
            printf("[SERVER1] DOWNLOAD: '%s' exists on both servers — copies DIFFER — sending both copies to client\n", path);
            send_u32(cfd, 2);
            send_u32(cfd, len1); if (len1 > 0) send_all(cfd, buf1, len1);
            send_u32(cfd, len2); if (len2 > 0) send_all(cfd, buf2, len2);
        }
    }

    free(buf1);
    free(buf2);
}

/* =================================================================
 * HANDLER: FILE_SIZE
 * Reply: [uint32_t s1_found][uint32_t s1_size][uint32_t s2_found][uint32_t s2_size]
 * ================================================================= */
static void handle_size(int cfd, const char *base_dir) {
    char path[MAX_PATH_LEN + 1];
    if (recv_string(cfd, path, sizeof(path)) != 0) {
        fprintf(stderr, "[SERVER1] FILE SIZE: failed to receive filename from client\n");
        return;
    }
    printf("[SERVER1] FILE SIZE request received from client — file: '%s'\n", path);

    /* Check local */
    uint32_t s1_found = 0, s1_size = 0;
    char full1[MAX_PATH_LEN + 1];
    if (build_fullpath(full1, sizeof(full1), base_dir, path) == 0) {
        struct stat st;
        if (stat(full1, &st) == 0 && S_ISREG(st.st_mode)) {
            s1_found = 1;
            s1_size = (uint32_t)st.st_size;
            printf("[SERVER1] FILE SIZE: '%s' found locally — %u bytes\n", path, s1_size);
        } else {
            printf("[SERVER1] FILE SIZE: '%s' not found locally\n", path);
        }
    }

    /* Ask SERVER2 */
    printf("[SERVER1] FILE SIZE: querying SERVER2 for '%s'...\n", path);
    uint32_t s2_found = 0, s2_size = 0;
    int s2 = s2_open(OP_SIZE);
    if (s2 >= 0) {
        if (send_string(s2, path) == 0) {
            recv_u32(s2, &s2_found);
            recv_u32(s2, &s2_size);
        }
        close(s2);
        if (s2_found)
            printf("[SERVER1] FILE SIZE: SERVER2 has '%s' — %u bytes\n", path, s2_size);
        else
            printf("[SERVER1] FILE SIZE: SERVER2 does not have '%s'\n", path);
    } else {
        printf("[SERVER1] FILE SIZE: SERVER2 unreachable — reporting SERVER1 data only\n");
    }

    /* Log combined result before replying */
    if (!s1_found && !s2_found) {
        printf("[SERVER1] FILE SIZE: '%s' not found on either server\n", path);
    } else if (s1_found && s2_found && s1_size == s2_size) {
        printf("[SERVER1] FILE SIZE: '%s' on both servers — sizes match (%u bytes) — sending to client\n",
               path, s1_size);
    } else if (s1_found && s2_found) {
        printf("[SERVER1] FILE SIZE: '%s' on both servers — sizes differ (S1:%u bytes  S2:%u bytes) — sending to client\n",
               path, s1_size, s2_size);
    } else if (s1_found) {
        printf("[SERVER1] FILE SIZE: '%s' on SERVER1 only (%u bytes) — sending to client\n", path, s1_size);
    } else {
        printf("[SERVER1] FILE SIZE: '%s' on SERVER2 only (%u bytes) — relaying to client\n", path, s2_size);
    }

    send_u32(cfd, s1_found); send_u32(cfd, s1_size);
    send_u32(cfd, s2_found); send_u32(cfd, s2_size);
}

/* =================================================================
 * HANDLER: CREATE
 * target: 0=both, 1=s1 only, 2=s2 only
 * Reply: [uint32_t s1_status][uint32_t s2_status]
 *   0=skipped, 1=ok, 2=error
 * ================================================================= */
static void handle_create(int cfd, const char *base_dir) {
    uint8_t target = 0;
    if (recv_all(cfd, &target, 1) != 0) {
        fprintf(stderr, "[SERVER1] CREATE: failed to recv target\n");
        return;
    }

    char path[MAX_PATH_LEN + 1];
    if (recv_string(cfd, path, sizeof(path)) != 0) {
        fprintf(stderr, "[SERVER1] CREATE: failed to recv path\n");
        return;
    }

    uint32_t clen = 0;
    if (recv_u32(cfd, &clen) != 0) {
        fprintf(stderr, "[SERVER1] CREATE: failed to recv content len\n");
        return;
    }

    uint8_t *content = NULL;
    if (clen > 0) {
        content = (uint8_t *)malloc(clen);
        if (!content) { send_u32(cfd, 2); send_u32(cfd, 2); return; }
        if (recv_all(cfd, content, clen) != 0) {
            free(content);
            fprintf(stderr, "[SERVER1] CREATE: failed to recv content\n");
            return;
        }
    }

    const char *target_label = (target == CREATE_BOTH) ? "both servers"
                             : (target == CREATE_S1)   ? "SERVER1 only"
                             :                           "SERVER2 only";
    printf("[SERVER1] CREATE request received from client — file: '%s'  target: %s  content: %u bytes\n",
           path, target_label, clen);

    uint32_t s1_status = 0, s2_status = 0;

    /* Create on SERVER1 if target is both or S1 */
    if (target == CREATE_BOTH || target == CREATE_S1) {
        printf("[SERVER1] CREATE: writing '%s' to local storage...\n", path);
        char full1[MAX_PATH_LEN + 1];
        if (build_fullpath(full1, sizeof(full1), base_dir, path) == 0) {
            int fd = open(full1, O_CREAT | O_TRUNC | O_WRONLY, 0644);
            if (fd >= 0) {
                s1_status = 1;
                if (clen > 0) {
                    uint32_t written = 0;
                    while (written < clen) {
                        ssize_t w = write(fd, content + written, clen - written);
                        if (w < 0) { if (errno == EINTR) continue; s1_status = 2; break; }
                        written += (uint32_t)w;
                    }
                }
                close(fd);
                if (s1_status == 1)
                    printf("[SERVER1] CREATE: '%s' written successfully to SERVER1 local storage\n", path);
                else
                    fprintf(stderr, "[SERVER1] CREATE: write error for '%s' on SERVER1\n", path);
            } else {
                s1_status = 2;
                fprintf(stderr, "[SERVER1] CREATE: failed to create '%s' on SERVER1 — %s\n",
                        path, strerror(errno));
            }
        } else {
            s1_status = 2;
            fprintf(stderr, "[SERVER1] CREATE: invalid path '%s' rejected\n", path);
        }
    }

    /* Forward create to SERVER2 if target is both or S2 */
    if (target == CREATE_BOTH || target == CREATE_S2) {
        printf("[SERVER1] CREATE: forwarding create request for '%s' to SERVER2...\n", path);
        int s2 = s2_open(OP_CREATE);
        if (s2 >= 0) {
            if (send_string(s2, path) == 0 &&
                send_u32(s2, clen) == 0 &&
                (clen == 0 || send_all(s2, content, clen) == 0)) {
                uint32_t res = 0;
                if (recv_u32(s2, &res) == 0) {
                    s2_status = res ? 1 : 2;
                    printf("[SERVER1] CREATE: SERVER2 reports '%s' — %s\n",
                           path, s2_status == 1 ? "written successfully" : "write failed");
                } else {
                    s2_status = 2;
                    fprintf(stderr, "[SERVER1] CREATE: no response from SERVER2 for '%s'\n", path);
                }
            } else {
                s2_status = 2;
                fprintf(stderr, "[SERVER1] CREATE: failed to send '%s' to SERVER2\n", path);
            }
            close(s2);
        } else {
            s2_status = 2;
            fprintf(stderr, "[SERVER1] CREATE: SERVER2 unreachable — could not create '%s' on SERVER2\n", path);
        }
    }

    printf("[SERVER1] CREATE: complete — SERVER1: %s  SERVER2: %s  — sending result to client\n",
           s1_status == 1 ? "ok" : s1_status == 0 ? "skipped" : "failed",
           s2_status == 1 ? "ok" : s2_status == 0 ? "skipped" : "failed");

    free(content);
    send_u32(cfd, s1_status);
    send_u32(cfd, s2_status);
}

/* =================================================================
 * HANDLER: LIST
 * target: 0=both, 1=s1, 2=s2
 * Reply:
 *   [uint32_t s1_count] ([uint32_t name_len][name][uint32_t size])...
 *   [uint32_t s2_count] ([uint32_t name_len][name][uint32_t size])...
 *   (s1_count or s2_count may be 0 if that server was not queried or has no files)
 * ================================================================= */
static void handle_list(int cfd, const char *base_dir) {
    uint8_t target = 0;
    if (recv_all(cfd, &target, 1) != 0) {
        fprintf(stderr, "[SERVER1] LIST: failed to recv target\n");
        return;
    }
    const char *list_target_label = (target == LIST_BOTH) ? "both servers"
                                  : (target == LIST_S1)   ? "SERVER1 only"
                                  :                         "SERVER2 only";
    printf("[SERVER1] LIST FILES request received from client — scope: %s\n", list_target_label);

    /* --- SERVER1 local file listing --- */
    if (target == LIST_BOTH || target == LIST_S1) {
        printf("[SERVER1] LIST: reading local file directory...\n");
        DIR *dp = opendir(base_dir);
        if (!dp) {
            fprintf(stderr, "[SERVER1] LIST: cannot open local directory '%s'\n", base_dir);
            send_u32(cfd, 0);
        } else {
            uint32_t count = 0;
            struct dirent *ent;
            while ((ent = readdir(dp)) != NULL) {
                if (ent->d_name[0] == '.') continue;
                char full[MAX_PATH_LEN + 1];
                snprintf(full, sizeof(full), "%s/%s", base_dir, ent->d_name);
                struct stat st;
                if (stat(full, &st) == 0 && S_ISREG(st.st_mode)) count++;
            }
            printf("[SERVER1] LIST: found %u file(s) in local storage\n", count);
            send_u32(cfd, count);
            rewinddir(dp);
            while ((ent = readdir(dp)) != NULL) {
                if (ent->d_name[0] == '.') continue;
                char full[MAX_PATH_LEN + 1];
                snprintf(full, sizeof(full), "%s/%s", base_dir, ent->d_name);
                struct stat st;
                if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
                uint32_t nlen = (uint32_t)strlen(ent->d_name);
                send_u32(cfd, nlen);
                send_all(cfd, ent->d_name, nlen);
                send_u32(cfd, (uint32_t)st.st_size);
            }
            closedir(dp);
        }
    } else {
        send_u32(cfd, 0);
        printf("[SERVER1] LIST: SERVER1 local listing skipped (client requested SERVER2 only)\n");
    }

    /* --- Forward list request to SERVER2 --- */
    if (target == LIST_BOTH || target == LIST_S2) {
        printf("[SERVER1] LIST: forwarding list request to SERVER2...\n");
        int s2 = s2_open(OP_LIST);
        if (s2 < 0) {
            fprintf(stderr, "[SERVER1] LIST: SERVER2 unreachable — sending empty list for SERVER2\n");
            send_u32(cfd, 0);
        } else {
            uint32_t s2count = 0;
            if (recv_u32(s2, &s2count) != 0) {
                close(s2);
                fprintf(stderr, "[SERVER1] LIST: failed to receive file count from SERVER2\n");
                send_u32(cfd, 0);
            } else {
                printf("[SERVER1] LIST: SERVER2 reported %u file(s) — relaying to client\n", s2count);
                send_u32(cfd, s2count);
                for (uint32_t i = 0; i < s2count; i++) {
                    uint32_t nlen = 0;
                    recv_u32(s2, &nlen);
                    send_u32(cfd, nlen);
                    char name[MAX_PATH_LEN + 1];
                    if (nlen > 0 && nlen < MAX_PATH_LEN) {
                        recv_all(s2, name, nlen);
                        send_all(cfd, name, nlen);
                    }
                    uint32_t sz = 0;
                    recv_u32(s2, &sz);
                    send_u32(cfd, sz);
                }
                close(s2);
            }
        }
    } else {
        send_u32(cfd, 0);
        printf("[SERVER1] LIST: SERVER2 listing skipped (client requested SERVER1 only)\n");
    }

    printf("[SERVER1] LIST: complete — full listing sent to client\n");
}

/* =================================================================
 * HANDLER: DELETE
 * First send the path to both servers, collect found status,
 * send back to client: [uint32_t s1_found][uint32_t s2_found]
 * Then receive from client: [uint8_t del_target: 0=both,1=s1,2=s2]
 * Execute deletions, reply: [uint32_t s1_del][uint32_t s2_del]
 *   0=notfound 1=deleted 2=error 3=skipped
 * ================================================================= */
static void handle_delete(int cfd, const char *base_dir) {
    char path[MAX_PATH_LEN + 1];
    if (recv_string(cfd, path, sizeof(path)) != 0) {
        fprintf(stderr, "[SERVER1] DELETE: failed to receive filename from client\n");
        return;
    }
    printf("[SERVER1] DELETE request received from client — file: '%s'\n", path);

    /* Check existence on SERVER1 */
    uint32_t s1_found = 0;
    char full1[MAX_PATH_LEN + 1];
    if (build_fullpath(full1, sizeof(full1), base_dir, path) == 0) {
        if (access(full1, F_OK) == 0) {
            s1_found = 1;
            printf("[SERVER1] DELETE: '%s' found in local storage\n", path);
        } else {
            printf("[SERVER1] DELETE: '%s' not found in local storage\n", path);
        }
    }

    /* Check existence on SERVER2 via a size probe (avoids reading file data) */
    printf("[SERVER1] DELETE: checking SERVER2 for '%s'...\n", path);
    uint32_t s2_found = 0;
    int s2 = s2_open(OP_SIZE);
    if (s2 >= 0) {
        if (send_string(s2, path) == 0) {
            uint32_t sf = 0, ss = 0;
            if (recv_u32(s2, &sf) == 0 && recv_u32(s2, &ss) == 0) s2_found = sf;
        }
        close(s2);
        printf("[SERVER1] DELETE: '%s' %s on SERVER2\n",
               path, s2_found ? "found" : "not found");
    } else {
        printf("[SERVER1] DELETE: SERVER2 unreachable — treating '%s' as not found on SERVER2\n", path);
    }

    /* Send combined found status to client so it can present the delete menu */
    send_u32(cfd, s1_found);
    send_u32(cfd, s2_found);

    if (!s1_found && !s2_found) {
        printf("[SERVER1] DELETE: '%s' not found on either server — notifying client\n", path);
        return;
    }

    /* Wait for client's deletion scope decision */
    uint8_t del_target = 0;
    if (recv_all(cfd, &del_target, 1) != 0) {
        fprintf(stderr, "[SERVER1] DELETE: client disconnected before confirming deletion scope for '%s'\n", path);
        return;
    }

    const char *scope_label = (del_target == 0) ? "both servers"
                            : (del_target == 1) ? "SERVER1 only"
                            : (del_target == 2) ? "SERVER2 only"
                            :                    "cancelled";
    printf("[SERVER1] DELETE: client confirmed — delete '%s' from %s\n", path, scope_label);

    uint32_t s1_del = 3, s2_del = 3;  /* 3 = skipped by default */

    /* Delete from SERVER1 local storage */
    if ((del_target == 0 && s1_found) || del_target == 1) {
        if (s1_found) {
            printf("[SERVER1] DELETE: removing '%s' from local storage...\n", path);
            if (unlink(full1) == 0) {
                s1_del = 1;
                printf("[SERVER1] DELETE: '%s' successfully removed from SERVER1 local storage\n", path);
            } else {
                s1_del = 2;
                fprintf(stderr, "[SERVER1] DELETE: failed to remove '%s' from SERVER1 — %s\n",
                        path, strerror(errno));
            }
        } else {
            s1_del = 0;
        }
    }

    /* Forward delete to SERVER2 */
    if ((del_target == 0 && s2_found) || del_target == 2) {
        if (s2_found) {
            printf("[SERVER1] DELETE: forwarding delete request for '%s' to SERVER2...\n", path);
            int s2d = s2_open(OP_DELETE);
            if (s2d >= 0) {
                if (send_string(s2d, path) == 0) {
                    uint32_t res = 0;
                    if (recv_u32(s2d, &res) == 0) {
                        s2_del = res;
                        printf("[SERVER1] DELETE: SERVER2 reports '%s' — %s\n",
                               path,
                               s2_del == 1 ? "successfully deleted"
                             : s2_del == 0 ? "file not found"
                             :               "delete failed");
                    } else {
                        s2_del = 2;
                        fprintf(stderr, "[SERVER1] DELETE: no response from SERVER2 for '%s'\n", path);
                    }
                } else {
                    s2_del = 2;
                    fprintf(stderr, "[SERVER1] DELETE: failed to send delete request to SERVER2 for '%s'\n", path);
                }
                close(s2d);
            } else {
                s2_del = 2;
                fprintf(stderr, "[SERVER1] DELETE: SERVER2 unreachable — could not delete '%s' from SERVER2\n", path);
            }
        } else {
            s2_del = 0;
        }
    }

    printf("[SERVER1] DELETE: complete — SERVER1: %s  SERVER2: %s  — sending result to client\n",
           s1_del == 1 ? "deleted" : s1_del == 0 ? "not found" : s1_del == 3 ? "skipped" : "failed",
           s2_del == 1 ? "deleted" : s2_del == 0 ? "not found" : s2_del == 3 ? "skipped" : "failed");

    send_u32(cfd, s1_del);
    send_u32(cfd, s2_del);
}

/* =================================================================
 * HANDLER: SHUTDOWN
 * Forward shutdown to SERVER2, then exit SERVER1
 * Reply: [uint32_t 0]
 * ================================================================= */
static void handle_shutdown(int cfd) {
    printf("[SERVER1] SHUTDOWN command received from client\n");

    /* Forward shutdown to SERVER2 first */
    printf("[SERVER1] SHUTDOWN: forwarding shutdown command to SERVER2...\n");
    int s2 = s2_open(OP_SHUTDOWN);
    if (s2 >= 0) {
        uint32_t res = 0;
        recv_u32(s2, &res);
        close(s2);
        printf("[SERVER1] SHUTDOWN: SERVER2 acknowledged shutdown — going offline\n");
    } else {
        printf("[SERVER1] SHUTDOWN: SERVER2 was already offline or unreachable\n");
    }

    printf("[SERVER1] SHUTDOWN: notifying client — going offline\n");
    send_u32(cfd, 0);
    g_shutdown = 1;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <listen_port> <server2_ip> <server2_port> <base_dir_server1>\n",
                argv[0]);
        return 1;
    }

    int listen_port  = atoi(argv[1]);
    g_s2_ip          = argv[2];
    g_s2_port        = atoi(argv[3]);
    const char *base_dir = argv[4];

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)listen_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind"); close(listen_fd); return 1;
    }
    if (listen(listen_fd, BACKLOG) != 0) {
        perror("listen"); close(listen_fd); return 1;
    }

    /* Ignore SIGPIPE so writes to disconnected clients don't crash the server */
    signal(SIGPIPE, SIG_IGN);

    printf("[SERVER1] Listening on port %d, base_dir=%s, server2=%s:%d\n",
           listen_port, base_dir, g_s2_ip, g_s2_port);
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

        uint8_t opcode = 0;
        if (recv_all(cfd, &opcode, 1) != 0) {
            fprintf(stderr, "[SERVER1] Failed to read opcode\n");
            close(cfd);
            continue;
        }

        switch (opcode) {
            case OP_PING:     handle_ping(cfd);              break;
            case OP_RETRIEVE: handle_retrieve(cfd, base_dir); break;
            case OP_SIZE:     handle_size(cfd, base_dir);    break;
            case OP_CREATE:   handle_create(cfd, base_dir);  break;
            case OP_LIST:     handle_list(cfd, base_dir);    break;
            case OP_DELETE:   handle_delete(cfd, base_dir);  break;
            case OP_SHUTDOWN: handle_shutdown(cfd);          break;
            default:
                fprintf(stderr, "[SERVER1] Unknown opcode: 0x%02x\n", opcode);
                break;
        }

        close(cfd);
    }

    close(listen_fd);
    printf("[SERVER1] Shutdown complete.\n");
    return 0;
}
