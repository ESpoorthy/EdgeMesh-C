#ifndef EDGEMESH_METRICS_H
#define EDGEMESH_METRICS_H

#include <stdint.h>

/* Ring-buffer and time-series limits. */
#define LATENCY_RING_SIZE  10000
#define TIME_SERIES_MAX    300

/* One time-series data point (populated by metrics_tick()). */
typedef struct {
    char     timestamp[24];          /* RFC3339 */
    uint64_t requests_total;
    uint64_t responses_forwarded_total;
    uint64_t proxy_failures_total;
    double   requests_per_second;    /* 0.0 if <2 complete buckets */
    double   response_bytes_per_second; /* 0.0 if <2 complete buckets */
    uint64_t active_requests;
} time_series_point_t;

/* Snapshot of all metrics for the admin API. */
typedef struct {
    uint64_t            requests_total;
    uint64_t            responses_forwarded_total;
    uint64_t            proxy_failures_total;
    int                 active_requests;
    double              p50_ms;           /* NAN if no samples */
    double              p95_ms;           /* NAN if no samples */
    double              p99_ms;           /* NAN if no samples */
    double              requests_per_second;        /* 0.0 if <2 buckets */
    double              response_bytes_per_second;  /* 0.0 if <2 buckets */
    time_series_point_t series[TIME_SERIES_MAX];
    int                 series_count;
} metrics_t;

/* Initialise the global metrics store. */
void metrics_init(void);

/*
 * metrics_record_request(success, bytes, latency_ms)
 *
 * Called from the proxy worker on completion of a proxied request.
 * success == 1: increment responses_forwarded_total and add bytes.
 * success == 0: increment proxy_failures_total.
 * latency_ms: pushed into the latency ring buffer.
 * active_requests is decremented after the request completes.
 * NEVER holds the lock during network I/O.
 * Health-probe traffic must NOT call this function.
 */
void metrics_record_request(int success, uint64_t bytes, double latency_ms);

/*
 * metrics_record_request_start()
 *
 * Increments active_requests when a proxy request begins.
 */
void metrics_record_request_start(void);

/*
 * metrics_tick()
 *
 * Called by a 1-second timer thread.  Snapshots counters, computes per-second
 * rates from the delta since the previous tick, appends to time_series
 * (rotating oldest when TIME_SERIES_MAX is reached).
 */
void metrics_tick(void);

/*
 * metrics_get_snapshot()
 *
 * Returns a copy of the current metrics state (safe to read without holding
 * the metrics lock after return).
 */
metrics_t metrics_get_snapshot(void);

/*
 * metrics_percentile(p)
 *
 * Returns the p-th percentile (0.0–100.0) from the latency ring buffer.
 * Copies and sorts the ring; does NOT modify global state.
 * Returns NAN if the ring is empty.
 */
double metrics_percentile(double p);

#endif /* EDGEMESH_METRICS_H */
