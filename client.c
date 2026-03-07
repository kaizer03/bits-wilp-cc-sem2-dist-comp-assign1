/*
 * client.c
 * --------
 * CLIENT: Requests a pathname from SERVER1 and receives:
 *  - status=0 => not found
 *  - status=1 => one file (len + bytes)
 *  - status=2 => two files (len1+bytes1, len2+bytes2)
 *
 * This client writes received files into current directory:
 *  - If one file:  received_<pathname>
 *  - If two files: received1_<pathname> and received2_<pathname>
 *
 * Usage:
 *   ./client <server1_ip> <server1_port> <pathname>
 *
 * Example:
 *   ./client 127.0.0.1 5001 a.txt
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
#include <fcntl.h>

#define MAX_PATH_LEN 4096

/* ---- recv exactly N bytes ---- */
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

/* ---- send exactly N bytes ---- */
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

/* ---- connect to SERVER1 ---- */
static int connect_to_server(const char *ip, int port) {
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

/* ---- send pathname ---- */
static int send_pathname(int sock, const char *path) {
    uint32_t len = (uint32_t)strlen(path);
    uint32_t nlen = htonl(len);

    if (len == 0 || len > MAX_PATH_LEN) {
        errno = EINVAL;
        return -1;
    }

    if (send_all(sock, &nlen, sizeof(nlen)) != 0) return -1;
    if (send_all(sock, path, len) != 0) return -1;
    return 0;
}

/* ---- write file bytes to disk ---- */
static int write_bytes_to_file(const char *filename, const uint8_t *buf, uint32_t len) {
    int fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) return -1;

    uint32_t total = 0;
    while (total < len) {
        ssize_t w = write(fd, buf + total, len - total);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        total += (uint32_t)w;
    }

    close(fd);
    return 0;
}

/* ---- make output filenames (avoid slashes) ---- */
static void sanitize_path(const char *in, char *out, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 1 < cap; i++) {
        char c = in[i];
        if (c == '/' || c == '\\') c = '_';
        out[j++] = c;
    }
    out[j] = '\0';
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server1_ip> <server1_port> <pathname>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    const char *path = argv[3];

    int fd = connect_to_server(ip, port);
    if (fd < 0) {
        perror("connect");
        return 1;
    }

    /* Send request */
    if (send_pathname(fd, path) != 0) {
        perror("send pathname");
        close(fd);
        return 1;
    }

    /* Receive status */
    uint32_t nstatus = 0;
    if (recv_all(fd, &nstatus, sizeof(nstatus)) != 0) {
        fprintf(stderr, "Failed to receive status\n");
        close(fd);
        return 1;
    }
    uint32_t status = ntohl(nstatus);

    char safe[MAX_PATH_LEN + 1];
    sanitize_path(path, safe, sizeof(safe));

    if (status == 0) {
        printf("[CLIENT] File '%s' NOT FOUND on servers.\n", path);
        close(fd);
        return 0;
    }

    if (status == 1) {
        uint32_t nlen = 0;
        if (recv_all(fd, &nlen, sizeof(nlen)) != 0) {
            fprintf(stderr, "Failed to receive file length\n");
            close(fd);
            return 1;
        }
        uint32_t len = ntohl(nlen);

        uint8_t *buf = NULL;
        if (len > 0) {
            buf = (uint8_t *)malloc(len);
            if (!buf) {
                fprintf(stderr, "Out of memory\n");
                close(fd);
                return 1;
            }
            if (recv_all(fd, buf, len) != 0) {
                fprintf(stderr, "Failed to receive file bytes\n");
                free(buf);
                close(fd);
                return 1;
            }
        }

        char outname[MAX_PATH_LEN + 32];
        snprintf(outname, sizeof(outname), "received_%s", safe);

        if (write_bytes_to_file(outname, buf, len) != 0) {
            fprintf(stderr, "Failed to write '%s' (%s)\n", outname, strerror(errno));
            free(buf);
            close(fd);
            return 1;
        }

        printf("[CLIENT] Received ONE file (%u bytes). Saved as '%s'\n", len, outname);
        free(buf);
        close(fd);
        return 0;
    }

    if (status == 2) {
        /* Receive file1 */
        uint32_t nlen1 = 0;
        if (recv_all(fd, &nlen1, sizeof(nlen1)) != 0) {
            fprintf(stderr, "Failed to receive file1 length\n");
            close(fd);
            return 1;
        }
        uint32_t len1 = ntohl(nlen1);

        uint8_t *buf1 = NULL;
        if (len1 > 0) {
            buf1 = (uint8_t *)malloc(len1);
            if (!buf1) {
                fprintf(stderr, "Out of memory\n");
                close(fd);
                return 1;
            }
            if (recv_all(fd, buf1, len1) != 0) {
                fprintf(stderr, "Failed to receive file1 bytes\n");
                free(buf1);
                close(fd);
                return 1;
            }
        }

        /* Receive file2 */
        uint32_t nlen2 = 0;
        if (recv_all(fd, &nlen2, sizeof(nlen2)) != 0) {
            fprintf(stderr, "Failed to receive file2 length\n");
            free(buf1);
            close(fd);
            return 1;
        }
        uint32_t len2 = ntohl(nlen2);

        uint8_t *buf2 = NULL;
        if (len2 > 0) {
            buf2 = (uint8_t *)malloc(len2);
            if (!buf2) {
                fprintf(stderr, "Out of memory\n");
                free(buf1);
                close(fd);
                return 1;
            }
            if (recv_all(fd, buf2, len2) != 0) {
                fprintf(stderr, "Failed to receive file2 bytes\n");
                free(buf1);
                free(buf2);
                close(fd);
                return 1;
            }
        }

        char out1[MAX_PATH_LEN + 32];
        char out2[MAX_PATH_LEN + 32];
        snprintf(out1, sizeof(out1), "received1_%s", safe);
        snprintf(out2, sizeof(out2), "received2_%s", safe);

        if (write_bytes_to_file(out1, buf1, len1) != 0) {
            fprintf(stderr, "Failed to write '%s' (%s)\n", out1, strerror(errno));
            free(buf1);
            free(buf2);
            close(fd);
            return 1;
        }
        if (write_bytes_to_file(out2, buf2, len2) != 0) {
            fprintf(stderr, "Failed to write '%s' (%s)\n", out2, strerror(errno));
            free(buf1);
            free(buf2);
            close(fd);
            return 1;
        }

        printf("[CLIENT] Received TWO files (different copies).\n");
        printf("         File1: %u bytes -> '%s'\n", len1, out1);
        printf("         File2: %u bytes -> '%s'\n", len2, out2);

        free(buf1);
        free(buf2);
        close(fd);
        return 0;
    }

    printf("[CLIENT] Unknown status code: %u\n", status);
    close(fd);
    return 1;
}