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
/* 6. metrics_record_client_reject does not inflate proxy_failures_total */
/* ------------------------------------------------------------------ */
static void test_client_reject_counter(void) {
    metrics_init();

    /* Simulate a client-side reject (no backend attempted). */
    metrics_record_request_start();
    metrics_record_client_reject(3.0);

    metrics_t snap = metrics_get_snapshot();
    check("client_reject: requests_total == 1",
          snap.requests_total == 1);
    check("client_reject: proxy_failures_total == 0",
          snap.proxy_failures_total == 0);
    check("client_reject: responses_forwarded_total == 0",
          snap.responses_forwarded_total == 0);
    check("client_reject: active_requests == 0",
          snap.active_requests == 0);
}

/* ------------------------------------------------------------------ */
/* 7. time_series does not exceed TIME_SERIES_MAX (300)                */
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
/* 8. requests_per_second uses rolling average, not last 1-sec spike   */
/* ------------------------------------------------------------------ */
static void test_rolling_rate(void) {
    metrics_init();

    /* Tick once to establish baseline (ticks_done=1, rate still 0.0). */
    metrics_tick();
    metrics_t snap0 = metrics_get_snapshot();
    check("rolling_rate: 0.0 after 1 tick", snap0.requests_per_second == 0.0);

    /* Record 10 requests then tick a second time. */
    for (int i = 0; i < 10; i++) {
        metrics_record_request_start();
        metrics_record_request(1, 100, 1.0);
    }
    metrics_tick();

    /* Record 20 requests then tick a third time. */
    for (int i = 0; i < 20; i++) {
        metrics_record_request_start();
        metrics_record_request(1, 100, 1.0);
    }
    metrics_tick();

    /*
     * After 3 ticks: series has 3 points.
     * Tick 1: ticks_done was 0 → rps=0.0 (not enough baseline yet).
     * Tick 2: ticks_done was 1 → rps=0.0 (spec: need >=2 complete buckets).
     *   prev is updated to 10 at end of tick 2.
     * Tick 3: ticks_done was 2 → delta = 30-10 = 20, rps=20.0.
     * Series: [0.0, 0.0, 20.0]. Rolling average = 20.0/3 ≈ 6.67.
     * Key assertion: snapshot value != 20.0 (not just last spike).
     */
    metrics_t snap = metrics_get_snapshot();
    double expected = (0.0 + 0.0 + 20.0) / 3.0;  /* ≈ 6.67 */
    check("rolling_rate: average not last spike",
          fabs(snap.requests_per_second - expected) < 0.1);
}

/* ------------------------------------------------------------------ */
int main(void) {
    test_empty_ring();
    test_single_sample();
    test_hundred_samples();
    test_counters();
    test_active_requests();
    test_client_reject_counter();
    test_rolling_rate();
    test_series_max();

    printf("test_metrics: all tests passed\n");
    return 0;
}
