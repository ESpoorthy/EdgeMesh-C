#include "edgemesh/io_utils.h"

#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * mem_find — portable memmem replacement (memmem requires _GNU_SOURCE).
 */
static const char *mem_find(const char *haystack, size_t hlen,
                            const char *needle,   size_t nlen) {
    if (nlen == 0)   return haystack;
    if (hlen < nlen) return NULL;
    size_t limit = hlen - nlen;
    for (size_t i = 0; i <= limit; i++) {
        if (memcmp(haystack + i, needle, nlen) == 0) {
            return haystack + i;
        }
    }
    return NULL;
}

/*
 * read_with_timeout — reads from fd into buf (up to max bytes) using a
 * select() + read() loop.  Stops early when "\r\n\r\n" is found (HTTP header
 * boundary).  Returns bytes accumulated on success, or -errno on failure.
 */
ssize_t read_with_timeout(int fd, char *buf, size_t max, int timeout_ms) {
    size_t total = 0;

    while (total < max) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(fd, &rset);

        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int rc = select(fd + 1, &rset, NULL, NULL, &tv);
        if (rc < 0) {
            return -errno;
        }
        if (rc == 0) {
            /* Timeout. */
            return -ETIMEDOUT;
        }

        ssize_t n = read(fd, buf + total, max - total);
        if (n < 0) {
            return -errno;
        }
        if (n == 0) {
            /* Peer closed connection. */
            if (total == 0) {
                return -ECONNRESET;
            }
            break;
        }
        total += (size_t)n;

        /* Check for end-of-HTTP-headers sentinel. */
        if (total >= 4) {
            size_t search_start = (total > (size_t)n + 3)
                                  ? total - (size_t)n - 3
                                  : 0;
            if (mem_find(buf + search_start, total - search_start,
                         "\r\n\r\n", 4) != NULL) {
                break;
            }
        }
    }

    return (ssize_t)total;
}

/*
 * write_all — loops over send() with MSG_NOSIGNAL until all len bytes are
 * sent.  Returns len on success, or -errno on failure.
 */
ssize_t write_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            return -errno;
        }
        if (n == 0) {
            return -ECONNRESET;
        }
        sent += (size_t)n;
    }

    return (ssize_t)len;
}
