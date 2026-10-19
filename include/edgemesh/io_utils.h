#ifndef EDGEMESH_IO_UTILS_H
#define EDGEMESH_IO_UTILS_H

#include <stddef.h>
#include <sys/types.h>

/*
 * read_with_timeout(fd, buf, max, timeout_ms)
 *
 * Reads from fd into buf (up to max bytes) in a select() + read() loop.
 * Stops when "\r\n\r\n" is found (HTTP header boundary) or max bytes have
 * been read.  Returns bytes read (>= 0) on success, or a negative errno-
 * style value on error/timeout.
 */
ssize_t read_with_timeout(int fd, char *buf, size_t max, int timeout_ms);

/*
 * write_all(fd, buf, len)
 *
 * Loops over send() with MSG_NOSIGNAL until all len bytes are written or an
 * error occurs.  Returns len on success, or a negative errno-style value on
 * error.
 */
ssize_t write_all(int fd, const char *buf, size_t len);

#endif /* EDGEMESH_IO_UTILS_H */
