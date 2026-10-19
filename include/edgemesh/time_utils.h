#ifndef EDGEMESH_TIME_UTILS_H
#define EDGEMESH_TIME_UTILS_H

#include <stddef.h>
#include <stdint.h>

/*
 * rfc3339_now(buf, len)
 *
 * Writes the current UTC time as "YYYY-MM-DDTHH:MM:SSZ" into buf.
 * Uses clock_gettime(CLOCK_REALTIME) + gmtime_r.  Safe for concurrent use.
 * buf must be at least 21 bytes.
 */
void rfc3339_now(char *buf, size_t len);

/*
 * monotonic_ms()
 *
 * Returns elapsed milliseconds from a monotonic clock (CLOCK_MONOTONIC).
 * Suitable for latency measurements.  Safe for concurrent use.
 */
uint64_t monotonic_ms(void);

#endif /* EDGEMESH_TIME_UTILS_H */
