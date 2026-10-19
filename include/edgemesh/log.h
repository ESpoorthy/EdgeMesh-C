#ifndef EDGEMESH_LOG_H
#define EDGEMESH_LOG_H

/*
 * Thread-safe structured logging to stderr with RFC3339 timestamp.
 *
 * Usage:
 *   log_info("server started on port %d", port);
 *   log_warn("retrying connection");        // no extra args: use log_info_s etc
 *   log_error("connect failed: %s", msg);
 *
 * For a plain string with no format arguments, pass it directly — the format
 * specifier "%s" is implicit via the macros below.
 */

/*
 * Internal entry point — do not call directly.
 */
void log_entry(const char *level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/*
 * Public macros.  We require at least one format argument so that the macros
 * are strictly C99/C17 compliant (no zero-argument VA_ARGS extension).
 * For a bare string with no substitutions, pass "" as the second arg or use
 * the _s variants below.
 */
#define log_info(fmt, ...)  log_entry("INFO",  fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...)  log_entry("WARN",  fmt, ##__VA_ARGS__)
#define log_error(fmt, ...) log_entry("ERROR", fmt, ##__VA_ARGS__)

/*
 * Strict-pedantic-safe wrappers for bare string literals (no extra args).
 * Use these when -Wpedantic rejects ##__VA_ARGS__ with an empty list.
 */
#define log_info_s(msg)   log_entry("INFO",  "%s", (msg))
#define log_warn_s(msg)   log_entry("WARN",  "%s", (msg))
#define log_error_s(msg)  log_entry("ERROR", "%s", (msg))

#endif /* EDGEMESH_LOG_H */
