#include "edgemesh/backend_pool.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "backend_pool: %s\n", message);
    return 1;
}

static void make_healthy(em_backend_pool *pool) {
    for (size_t i = 0U; i < EM_BACKEND_COUNT; ++i) {
        em_backend_pool_probe_result(pool, i, true, 1000, 0.5);
        em_backend_pool_probe_result(pool, i, true, 1000, 0.5);
    }
}

int main(void) {
    int failed = 0;
    em_backend_pool pool;
    if (em_backend_pool_init(&pool, 19001U, EM_POLICY_ROUND_ROBIN) != 0) return fail("init failed");
    if (em_backend_pool_select_reserve(&pool, NULL) != -1) failed += fail("unknown backend was selected");
    make_healthy(&pool);

    int a = em_backend_pool_select_reserve(&pool, NULL);
    int b = em_backend_pool_select_reserve(&pool, NULL);
    int c = em_backend_pool_select_reserve(&pool, NULL);
    if (a != 0 || b != 1 || c != 2) failed += fail("round robin order is not 0,1,2");
    em_backend_pool_release(&pool, 0U);
    em_backend_pool_release(&pool, 1U);
    em_backend_pool_release(&pool, 2U);

    bool attempted[EM_BACKEND_COUNT] = { true, false, false };
    if (em_backend_pool_select_reserve(&pool, attempted) != 1) failed += fail("retry did not skip an attempted backend");
    em_backend_pool_release(&pool, 1U);

    em_backend_pool_probe_result(&pool, 1U, false, 2000, 1.0);
    em_backend_pool_probe_result(&pool, 1U, false, 2000, 1.0);
    em_backend_record snapshot;
    (void)em_backend_pool_snapshot(&pool, 1U, &snapshot);
    if (snapshot.state != EM_BACKEND_HEALTHY) failed += fail("backend failed before three consecutive failed probes");
    em_backend_pool_probe_result(&pool, 1U, false, 2000, 1.0);
    (void)em_backend_pool_snapshot(&pool, 1U, &snapshot);
    if (snapshot.state != EM_BACKEND_UNHEALTHY) failed += fail("backend did not become unhealthy after three failures");
    if (!snapshot.has_probe_result || snapshot.last_check_unix_ms != 2000 ||
        snapshot.last_probe_ms != 1.0) failed += fail("probe timestamp/latency were not recorded");
    if (em_backend_pool_select_reserve(&pool, NULL) == 1) failed += fail("unhealthy backend was selected");
    em_backend_pool_probe_result(&pool, 1U, true, 3000, 1.5);
    (void)em_backend_pool_snapshot(&pool, 1U, &snapshot);
    if (snapshot.state != EM_BACKEND_UNHEALTHY) failed += fail("backend recovered after only one successful probe");
    em_backend_pool_probe_result(&pool, 1U, true, 3000, 1.5);
    (void)em_backend_pool_snapshot(&pool, 1U, &snapshot);
    if (snapshot.state != EM_BACKEND_HEALTHY) failed += fail("backend did not recover after two successful probes");
    em_backend_pool_destroy(&pool);

    if (em_backend_pool_init(&pool, 19001U, EM_POLICY_LEAST_CONNECTIONS) != 0) return fail("least-connections init failed");
    make_healthy(&pool);
    a = em_backend_pool_select_reserve(&pool, NULL);
    b = em_backend_pool_select_reserve(&pool, NULL);
    c = em_backend_pool_select_reserve(&pool, NULL);
    int d = em_backend_pool_select_reserve(&pool, NULL);
    if (a != 0 || b != 1 || c != 2 || d != 0) failed += fail("least-connections did not reserve the lowest active count and rotate ties");
    (void)em_backend_pool_snapshot(&pool, 0U, &snapshot);
    if (snapshot.active_connections != 2U) failed += fail("reservation count does not match in-flight selection");
    em_backend_pool_release(&pool, 0U);
    em_backend_pool_release(&pool, 0U);
    em_backend_pool_release(&pool, 1U);
    em_backend_pool_release(&pool, 2U);
    (void)em_backend_pool_snapshot(&pool, 0U, &snapshot);
    if (snapshot.active_connections != 0U) failed += fail("reservation was not released");
    em_backend_pool_destroy(&pool);

    return failed == 0 ? 0 : 1;
}
