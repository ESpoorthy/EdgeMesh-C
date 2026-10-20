#ifndef EDGEMESH_BACKEND_POOL_H
#define EDGEMESH_BACKEND_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#define EM_BACKEND_COUNT 3U

typedef enum {
    EM_BACKEND_UNKNOWN = 0,
    EM_BACKEND_HEALTHY,
    EM_BACKEND_UNHEALTHY
} em_backend_state;

typedef enum {
    EM_POLICY_ROUND_ROBIN = 0,
    EM_POLICY_LEAST_CONNECTIONS
} em_policy;

typedef struct {
    char id[16];
    unsigned short port;
    em_backend_state state;
    unsigned consecutive_successes;
    unsigned consecutive_failures;
    size_t active_connections;
    uint64_t attempts;
    uint64_t attempt_failures;
    int64_t last_check_unix_ms;
    double last_probe_ms;
    bool has_probe_result;
} em_backend_record;

typedef struct {
    pthread_mutex_t mutex;
    bool mutex_ready;
    em_backend_record backends[EM_BACKEND_COUNT];
    em_policy policy;
    size_t round_robin_cursor;
    size_t tie_cursor;
} em_backend_pool;

int em_backend_pool_init(em_backend_pool *pool, unsigned short base_port, em_policy policy);
void em_backend_pool_destroy(em_backend_pool *pool);
/* Atomically selects an eligible backend and reserves its in-flight slot.
 * attempted may be NULL, otherwise true entries are skipped for retries.
 * Returns the backend index, or -1 if no healthy candidate is available. */
int em_backend_pool_select_reserve(em_backend_pool *pool, const bool attempted[EM_BACKEND_COUNT]);
void em_backend_pool_release(em_backend_pool *pool, size_t index);
void em_backend_pool_record_failure(em_backend_pool *pool, size_t index);
void em_backend_pool_probe_result(em_backend_pool *pool, size_t index, bool success,
                                  int64_t checked_at_unix_ms, double probe_ms);
int em_backend_pool_snapshot(em_backend_pool *pool, size_t index, em_backend_record *out);

#endif
