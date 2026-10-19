#include "edgemesh/time_utils.h"

#include <stdio.h>
#include <time.h>

/*
 * rfc3339_now — writes current UTC time as "YYYY-MM-DDTHH:MM:SSZ" into buf.
 * buf must be at least 21 bytes.  Safe for concurrent use (no shared state).
 */
void rfc3339_now(char *buf, size_t len) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm_buf;
    gmtime_r(&ts.tv_sec, &tm_buf);

    /* "YYYY-MM-DDTHH:MM:SSZ" is exactly 20 chars + NUL = 21 bytes. */
    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm_buf.tm_year + 1900,
             tm_buf.tm_mon  + 1,
             tm_buf.tm_mday,
             tm_buf.tm_hour,
             tm_buf.tm_min,
             tm_buf.tm_sec);
}

/*
 * monotonic_ms — returns elapsed milliseconds from CLOCK_MONOTONIC.
 * Safe for concurrent use (no shared state).
 */
uint64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000);
}
