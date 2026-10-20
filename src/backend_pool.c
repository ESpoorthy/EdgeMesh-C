#include "edgemesh/backend_pool.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static bool skipped(const bool attempted[EM_BACKEND_COUNT], size_t index) {
    return attempted != NULL && attempted[index];
}

int em_backend_pool_init(em_backend_pool *pool, unsigned short base_port, em_policy policy) {
    if (pool == NULL || base_port == 0U ||
        (unsigned)base_port + (EM_BACKEND_COUNT - 1U) > 65535U ||
        (policy != EM_POLICY_ROUND_ROBIN && policy != EM_POLICY_LEAST_CONNECTIONS)) {
        return -1;
    }
    memset(pool, 0, sizeof(*pool));
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) return -1;
    pool->mutex_ready = true;
    pool->policy = policy;
    for (size_t i = 0U; i < EM_BACKEND_COUNT; ++i) {
        (void)snprintf(pool->backends[i].id, sizeof(pool->backends[i].id),
                       "backend-%u", (unsigned)(i + 1U));
        pool->backends[i].port = (unsigned short)((unsigned)base_port + (unsigned)i);
        pool->backends[i].state = EM_BACKEND_UNKNOWN;
    }
    return 0;
}

void em_backend_pool_destroy(em_backend_pool *pool) {
    if (pool != NULL && pool->mutex_ready) {
        (void)pthread_mutex_destroy(&pool->mutex);
        pool->mutex_ready = false;
    }
}

int em_backend_pool_select_reserve(em_backend_pool *pool, const bool attempted[EM_BACKEND_COUNT]) {
    if (pool == NULL || !pool->mutex_ready) return -1;
    if (pthread_mutex_lock(&pool->mutex) != 0) return -1;

    int selected = -1;
    if (pool->policy == EM_POLICY_ROUND_ROBIN) {
        for (size_t offset = 0U; offset < EM_BACKEND_COUNT; ++offset) {
            size_t index = (pool->round_robin_cursor + offset) % EM_BACKEND_COUNT;
            if (pool->backends[index].state == EM_BACKEND_HEALTHY && !skipped(attempted, index)) {
                selected = (int)index;
                pool->round_robin_cursor = (index + 1U) % EM_BACKEND_COUNT;
                break;
            }
        }
    } else {
        size_t minimum = SIZE_MAX;
        for (size_t offset = 0U; offset < EM_BACKEND_COUNT; ++offset) {
            size_t index = (pool->tie_cursor + offset) % EM_BACKEND_COUNT;
            em_backend_record *backend = &pool->backends[index];
            if (backend->state != EM_BACKEND_HEALTHY || skipped(attempted, index)) continue;
            if (selected < 0 || backend->active_connections < minimum) {
                selected = (int)index;
                minimum = backend->active_connections;
            }
        }
        if (selected >= 0) pool->tie_cursor = ((size_t)selected + 1U) % EM_BACKEND_COUNT;
    }

    if (selected >= 0) {
        em_backend_record *backend = &pool->backends[(size_t)selected];
        if (backend->active_connections < SIZE_MAX) ++backend->active_connections;
        if (backend->attempts < UINT64_MAX) ++backend->attempts;
    }
    (void)pthread_mutex_unlock(&pool->mutex);
    return selected;
}

void em_backend_pool_release(em_backend_pool *pool, size_t index) {
    if (pool == NULL || !pool->mutex_ready || index >= EM_BACKEND_COUNT) return;
    if (pthread_mutex_lock(&pool->mutex) != 0) return;
    if (pool->backends[index].active_connections > 0U) --pool->backends[index].active_connections;
    (void)pthread_mutex_unlock(&pool->mutex);
}

void em_backend_pool_record_failure(em_backend_pool *pool, size_t index) {
    if (pool == NULL || !pool->mutex_ready || index >= EM_BACKEND_COUNT) return;
    if (pthread_mutex_lock(&pool->mutex) != 0) return;
    if (pool->backends[index].attempt_failures < UINT64_MAX) ++pool->backends[index].attempt_failures;
    (void)pthread_mutex_unlock(&pool->mutex);
}

void em_backend_pool_probe_result(em_backend_pool *pool, size_t index, bool success,
                                  int64_t checked_at_unix_ms, double probe_ms) {
    if (pool == NULL || !pool->mutex_ready || index >= EM_BACKEND_COUNT) return;
    if (pthread_mutex_lock(&pool->mutex) != 0) return;
    em_backend_record *backend = &pool->backends[index];
    backend->last_check_unix_ms = checked_at_unix_ms;
    backend->last_probe_ms = probe_ms;
    backend->has_probe_result = true;
    if (success) {
        backend->consecutive_failures = 0U;
        if (backend->consecutive_successes < UINT_MAX) ++backend->consecutive_successes;
        if (backend->consecutive_successes >= 2U) backend->state = EM_BACKEND_HEALTHY;
    } else {
        backend->consecutive_successes = 0U;
        if (backend->consecutive_failures < UINT_MAX) ++backend->consecutive_failures;
        if (backend->consecutive_failures >= 3U) backend->state = EM_BACKEND_UNHEALTHY;
    }
    (void)pthread_mutex_unlock(&pool->mutex);
}

int em_backend_pool_snapshot(em_backend_pool *pool, size_t index, em_backend_record *out) {
    if (pool == NULL || out == NULL || !pool->mutex_ready || index >= EM_BACKEND_COUNT) return -1;
    if (pthread_mutex_lock(&pool->mutex) != 0) return -1;
    *out = pool->backends[index];
    (void)pthread_mutex_unlock(&pool->mutex);
    return 0;
}
