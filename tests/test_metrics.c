#include "edgemesh/metrics.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void check(const char *label, int cond) {
    if (!cond) {
        fprintf(stderr, "FAIL [%s]\n", label);
    } else {
        printf("PASS [%s]\n", label);
    }
    assert(cond);
}

/* ------------------------------------------------------------------ */
/* 1. Empty ring → NAN for all percentiles                             */
/* ------------------------------------------------------------------ */
static void test_empty_ring(void) {
    metrics_init();
    double p50 = metrics_percentile(50.0);
    double p95 = metrics_percentile(95.0);
    double p99 = metrics_percentile(99.0);
    check("empty: p50 is NAN", isnan(p50));
    check("empty: p95 is NAN", isnan(p95));
    check("empty: p99 is NAN", isnan(p99));
}

/* ------------------------------------------------------------------ */
/* 2. After 1 sample, p50 == p95 == p99 == that sample                */
/* ------------------------------------------------------------------ */
static void test_single_sample(void) {
    metrics_init();
    metrics_record_request_start();
    metrics_record_request(1, 100, 42.0);

    double p50 = metrics_percentile(50.0);
    double p95 = metrics_percentile(95.0);
    double p99 = metrics_percentile(99.0);
    check("single: p50 == 42.0", fabs(p50 - 42.0) < 0.001);
    check("single: p95 == 42.0", fabs(p95 - 42.0) < 0.001);
    check("single: p99 == 42.0", fabs(p99 - 42.0) < 0.001);
}

/* ------------------------------------------------------------------ */
/* 3. 100 samples [0..99]: p50≈49.5, p95≈94.5, p99≈98.5 (within 1.0) */
/* ------------------------------------------------------------------ */
static void test_hundred_samples(void) {
    metrics_init();
    for (int i = 0; i < 100; i++) {
        metrics_record_request_start();
        metrics_record_request(1, 100, (double)i);
    }
    double p50 = metrics_percentile(50.0);
    double p95 = metrics_percentile(95.0);
    double p99 = metrics_percentile(99.0);
    check("hundred: p50 near 49.5",  fabs(p50 - 49.5) <= 1.0);
    check("hundred: p95 near 94.5",  fabs(p95 - 94.5) <= 1.0);
    check("hundred: p99 near 98.5",  fabs(p99 - 98.5) <= 1.0);
}

/* ------------------------------------------------------------------ */
/* 4. metrics_record_request increments correct counters               */
/* ------------------------------------------------------------------ */
static void test_counters(void) {
    metrics_init();

    metrics_record_request_start();
    metrics_record_request(1, 512, 10.0);   /* success */
    metrics_record_request_start();
    metrics_record_request(0, 0, 5.0);      /* failure */

    metrics_t snap = metrics_get_snapshot();
    check("counters: requests_total == 2",
          snap.requests_total == 2);
    check("counters: responses_forwarded_total == 1",
          snap.responses_forwarded_total == 1);
    check("counters: proxy_failures_total == 1",
          snap.proxy_failures_total == 1);
}

/* ------------------------------------------------------------------ */
/* 5. active_requests goes up then down                                */
/* ------------------------------------------------------------------ */
static void test_active_requests(void) {
    metrics_init();

    metrics_record_request_start();
    metrics_t snap1 = metrics_get_snapshot();
    check("active: +1 after start", snap1.active_requests == 1);

    metrics_record_request(1, 100, 1.0);
    metrics_t snap2 = metrics_get_snapshot();
    check("active: 0 after completion", snap2.active_requests == 0);
}

/* ------------------------------------------------------------------ */
/* 6. time_series does not exceed TIME_SERIES_MAX (300)                */
/* ------------------------------------------------------------------ */
static void test_series_max(void) {
    metrics_init();
    for (int i = 0; i < 400; i++) {
        metrics_tick();
    }
    metrics_t snap = metrics_get_snapshot();
    check("series_max: count <= TIME_SERIES_MAX",
          snap.series_count <= TIME_SERIES_MAX);
    check("series_max: count == TIME_SERIES_MAX after 400 ticks",
          snap.series_count == TIME_SERIES_MAX);
}

/* ------------------------------------------------------------------ */
int main(void) {
    test_empty_ring();
    test_single_sample();
    test_hundred_samples();
    test_counters();
    test_active_requests();
    test_series_max();

    printf("test_metrics: all tests passed\n");
    return 0;
}
