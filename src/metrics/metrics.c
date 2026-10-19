#include "edgemesh/metrics.h"
#include "edgemesh/time_utils.h"

#include <math.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>

/* --------------------------------------------------------------------- */
/* Internal state                                                          */
/* --------------------------------------------------------------------- */
typedef struct {
    /* Cumulative counters. */
    uint64_t requests_total;
    uint64_t responses_forwarded_total;
    uint64_t proxy_failures_total;
    uint64_t response_bytes_total;

    /* In-flight count. */
    int active_requests;

    /* Latency ring buffer. */
    double   latency_ring[LATENCY_RING_SIZE];
    int      ring_head;   /* next write position */
    int      ring_count;  /* number of valid samples (0..LATENCY_RING_SIZE) */

    /* Time-series (rotating). */
    time_series_point_t series[TIME_SERIES_MAX];
    int                 series_head;  /* next write position */
    int                 series_count; /* 0..TIME_SERIES_MAX */

    /* Snapshot of counters at the previous tick (for delta-based rates). */
    uint64_t prev_requests_total;
    uint64_t prev_bytes_total;
    int      ticks_done; /* how many ticks have fired — need >=2 for rates */
} internal_metrics_t;

static internal_metrics_t s_m;
static pthread_mutex_t    s_mutex = PTHREAD_MUTEX_INITIALIZER;

/* --------------------------------------------------------------------- */
/* Public: metrics_init                                                    */
/* --------------------------------------------------------------------- */
void metrics_init(void) {
    pthread_mutex_lock(&s_mutex);
    memset(&s_m, 0, sizeof(s_m));
    pthread_mutex_unlock(&s_mutex);
}

/* --------------------------------------------------------------------- */
/* Public: metrics_record_request_start                                    */
/* --------------------------------------------------------------------- */
void metrics_record_request_start(void) {
    pthread_mutex_lock(&s_mutex);
    s_m.active_requests++;
    pthread_mutex_unlock(&s_mutex);
}

/* --------------------------------------------------------------------- */
/* Public: metrics_record_request                                          */
/* --------------------------------------------------------------------- */
void metrics_record_request(int success, uint64_t bytes, double latency_ms) {
    pthread_mutex_lock(&s_mutex);

    s_m.requests_total++;
    if (s_m.active_requests > 0) {
        s_m.active_requests--;
    }

    if (success) {
        s_m.responses_forwarded_total++;
        s_m.response_bytes_total += bytes;
    } else {
        s_m.proxy_failures_total++;
    }

    /* Push latency sample into ring buffer (wrap on overflow). */
    s_m.latency_ring[s_m.ring_head] = latency_ms;
    s_m.ring_head = (s_m.ring_head + 1) % LATENCY_RING_SIZE;
    if (s_m.ring_count < LATENCY_RING_SIZE) {
        s_m.ring_count++;
    }

    pthread_mutex_unlock(&s_mutex);
}

/* --------------------------------------------------------------------- */
/* Public: metrics_record_client_reject                                    */
/* --------------------------------------------------------------------- */
/*
 * Called when a client request is rejected before any backend is selected.
 * Increments requests_total and decrements active_requests to keep the
 * in-flight count balanced, but does NOT increment proxy_failures_total
 * because no backend attempt was made.
 */
void metrics_record_client_reject(double latency_ms) {
    pthread_mutex_lock(&s_mutex);

    s_m.requests_total++;
    if (s_m.active_requests > 0) {
        s_m.active_requests--;
    }

    /* Push latency sample into ring buffer (wrap on overflow). */
    s_m.latency_ring[s_m.ring_head] = latency_ms;
    s_m.ring_head = (s_m.ring_head + 1) % LATENCY_RING_SIZE;
    if (s_m.ring_count < LATENCY_RING_SIZE) {
        s_m.ring_count++;
    }

    pthread_mutex_unlock(&s_mutex);
}

/* --------------------------------------------------------------------- */
/* Public: metrics_tick — called by the 1-second timer thread             */
/* --------------------------------------------------------------------- */
void metrics_tick(void) {
    pthread_mutex_lock(&s_mutex);

    char ts[24];
    rfc3339_now(ts, sizeof(ts));

    /*
     * Compute per-second rates from delta.
     * The spec requires 0.0 when fewer than 2 complete buckets exist,
     * so we need ticks_done >= 2 (the first tick establishes the baseline,
     * the second tick can compute a meaningful one-second delta).
     */
    double rps   = 0.0;
    double bps   = 0.0;
    if (s_m.ticks_done >= 2) {
        uint64_t req_delta   = s_m.requests_total - s_m.prev_requests_total;
        uint64_t bytes_delta = s_m.response_bytes_total - s_m.prev_bytes_total;
        rps  = (double)req_delta;
        bps  = (double)bytes_delta;
    }

    s_m.prev_requests_total = s_m.requests_total;
    s_m.prev_bytes_total    = s_m.response_bytes_total;
    s_m.ticks_done++;

    /* Snapshot counter values for the time-series point while locked. */
    uint64_t snap_req_total   = s_m.requests_total;
    uint64_t snap_fwd_total   = s_m.responses_forwarded_total;
    uint64_t snap_fail_total  = s_m.proxy_failures_total;
    uint64_t snap_active      = (uint64_t)s_m.active_requests;
    int      series_head      = s_m.series_head;

    /* Advance rotating write head before releasing lock. */
    s_m.series_head = (s_m.series_head + 1) % TIME_SERIES_MAX;
    if (s_m.series_count < TIME_SERIES_MAX) {
        s_m.series_count++;
    }

    pthread_mutex_unlock(&s_mutex);

    /*
     * Compute latency percentiles without holding the lock
     * (metrics_percentile() acquires its own lock internally).
     */
    double p50 = metrics_percentile(50.0);
    double p95 = metrics_percentile(95.0);
    double p99 = metrics_percentile(99.0);

    /* Build the time-series point (no lock needed — we own this slot). */
    time_series_point_t *pt = &s_m.series[series_head];
    memset(pt, 0, sizeof(*pt));
    strncpy(pt->timestamp, ts, sizeof(pt->timestamp) - 1);
    pt->requests_total              = snap_req_total;
    pt->responses_forwarded_total   = snap_fwd_total;
    pt->proxy_failures_total        = snap_fail_total;
    pt->requests_per_second         = rps;
    pt->response_bytes_per_second   = bps;
    pt->active_requests             = snap_active;
    pt->latency_p50_ms              = p50;
    pt->latency_p95_ms              = p95;
    pt->latency_p99_ms              = p99;
}

/* --------------------------------------------------------------------- */
/* Public: metrics_get_snapshot                                            */
/* --------------------------------------------------------------------- */
metrics_t metrics_get_snapshot(void) {
    metrics_t snap;
    memset(&snap, 0, sizeof(snap));

    pthread_mutex_lock(&s_mutex);

    snap.requests_total             = s_m.requests_total;
    snap.responses_forwarded_total  = s_m.responses_forwarded_total;
    snap.proxy_failures_total       = s_m.proxy_failures_total;
    snap.active_requests            = s_m.active_requests;

    /* Copy time-series in chronological order. */
    snap.series_count = s_m.series_count;
    if (s_m.series_count < TIME_SERIES_MAX) {
        /* Not yet wrapped: series[0..count-1] are in order. */
        memcpy(snap.series, s_m.series,
               (size_t)s_m.series_count * sizeof(time_series_point_t));
    } else {
        /* Wrapped: oldest entry is at series_head. */
        int oldest = s_m.series_head; /* next write pos == oldest after wrap */
        int first_chunk = TIME_SERIES_MAX - oldest;
        memcpy(snap.series,
               s_m.series + oldest,
               (size_t)first_chunk * sizeof(time_series_point_t));
        memcpy(snap.series + first_chunk,
               s_m.series,
               (size_t)oldest * sizeof(time_series_point_t));
    }

    pthread_mutex_unlock(&s_mutex);

    /* Compute percentiles without holding the lock. */
    snap.p50_ms = metrics_percentile(50.0);
    snap.p95_ms = metrics_percentile(95.0);
    snap.p99_ms = metrics_percentile(99.0);

    /* Compute rates as a rolling window average over all series points.
     * Each time_series_point_t.requests_per_second holds the 1-second
     * delta for that tick.  Summing all deltas and dividing by the
     * number of points gives the average requests/second over the window,
     * as required by api-contract §9 ("rolling window of per-second
     * buckets over sample_window_seconds").  Return 0.0 when <2 complete
     * buckets have been recorded (spec requirement). */
    if (snap.series_count >= 2) {
        double rps_sum  = 0.0;
        double bps_sum  = 0.0;
        for (int i = 0; i < snap.series_count; i++) {
            rps_sum += snap.series[i].requests_per_second;
            bps_sum += snap.series[i].response_bytes_per_second;
        }
        snap.requests_per_second       = rps_sum / (double)snap.series_count;
        snap.response_bytes_per_second = bps_sum / (double)snap.series_count;
    } else {
        snap.requests_per_second       = 0.0;
        snap.response_bytes_per_second = 0.0;
    }

    return snap;
}

/* --------------------------------------------------------------------- */
/* compare_double helper for qsort                                         */
/* --------------------------------------------------------------------- */
static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return  1;
    return 0;
}

/* --------------------------------------------------------------------- */
/* Public: metrics_percentile                                              */
/* --------------------------------------------------------------------- */
double metrics_percentile(double p) {
    pthread_mutex_lock(&s_mutex);
    int count = s_m.ring_count;
    if (count == 0) {
        pthread_mutex_unlock(&s_mutex);
        return NAN;
    }

    /* Copy the ring buffer (which may wrap) into a temporary array. */
    double *tmp = malloc((size_t)count * sizeof(double));
    if (tmp == NULL) {
        pthread_mutex_unlock(&s_mutex);
        return NAN;
    }

    /*
     * ring_head points to the next write slot.  The oldest sample is at
     * (ring_head - ring_count + LATENCY_RING_SIZE) % LATENCY_RING_SIZE
     * when the buffer is full, or simply at 0 when it hasn't wrapped yet.
     */
    if (count < LATENCY_RING_SIZE) {
        /* Buffer hasn't wrapped: indices 0..(count-1) are valid. */
        memcpy(tmp, s_m.latency_ring, (size_t)count * sizeof(double));
    } else {
        /* Buffer is full and may have wrapped. */
        int oldest = s_m.ring_head; /* ring_head == next-write == oldest */
        int first  = LATENCY_RING_SIZE - oldest;
        memcpy(tmp,         s_m.latency_ring + oldest,
               (size_t)first  * sizeof(double));
        memcpy(tmp + first, s_m.latency_ring,
               (size_t)oldest * sizeof(double));
    }

    pthread_mutex_unlock(&s_mutex);

    qsort(tmp, (size_t)count, sizeof(double), cmp_double);

    /* Linear interpolation. */
    double idx = (p / 100.0) * (double)(count - 1);
    int    lo  = (int)idx;
    double frac = idx - (double)lo;
    double result;
    if (lo + 1 < count) {
        result = tmp[lo] + frac * (tmp[lo + 1] - tmp[lo]);
    } else {
        result = tmp[lo];
    }

    free(tmp);
    return result;
}
