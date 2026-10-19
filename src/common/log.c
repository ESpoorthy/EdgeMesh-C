#include "edgemesh/log.h"
#include "edgemesh/time_utils.h"

#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>

static pthread_mutex_t s_log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_entry(const char *level, const char *fmt, ...) {
    char ts[24];
    rfc3339_now(ts, sizeof(ts));

    va_list ap;
    va_start(ap, fmt);

    /* Lock around the entire fprintf sequence so lines are not interleaved. */
    pthread_mutex_lock(&s_log_mutex);
    flockfile(stderr);

    fprintf(stderr, "[%s] [%s] ", ts, level);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");

    funlockfile(stderr);
    pthread_mutex_unlock(&s_log_mutex);

    va_end(ap);
}
