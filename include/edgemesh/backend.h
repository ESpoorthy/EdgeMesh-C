#ifndef EDGEMESH_BACKEND_H
#define EDGEMESH_BACKEND_H

#include <stdint.h>
#include <pthread.h>

/* Number of backends in the fixed pool. */
#define BACKEND_POOL_SIZE 3

/* Health state of a single backend. */
typedef enum {
    BACKEND_UNKNOWN   = 0,
    BACKEND_HEALTHY   = 1,
    BACKEND_UNHEALTHY = 2
} backend_state_t;

/* Load-balancing policy. */
typedef enum {
    POLICY_ROUND_ROBIN       = 0,
    POLICY_LEAST_CONNECTIONS = 1
} lb_policy_t;

/* Per-backend counters and state. */
typedef struct {
    int              id;                /* 0-based index */
    char             address[64];       /* e.g. "127.0.0.1" */
    int              port;              /* e.g. 9001 */
    backend_state_t  state;
    int              active_connections; /* in-flight proxy requests */
    uint64_t         requests_total;
    uint64_t         responses_forwarded;
    uint64_t         proxy_failures;
    char             last_check_at[24]; /* RFC3339 timestamp or "" */
    uint64_t         last_response_ms;
    int              consec_failures;
    int              consec_successes;
} backend_t;

/* The shared pool of backends, policy, and a single mutex. */
typedef struct {
    backend_t      backends[BACKEND_POOL_SIZE];
    lb_policy_t    policy;
    pthread_mutex_t mutex;        /* protects all fields above */
    pthread_mutex_t policy_mutex; /* additional mutex for atomic policy swap */
    int            last_rr_idx;  /* round-robin rotation state */
} backend_pool_t;

/* Global pool — defined in pool.c, declared here for all modules. */
extern backend_pool_t g_pool;

/* Initialise the global pool with default backends at :9001/:9002/:9003. */
void pool_init(void);

/* Lock / unlock helpers (wrap pthread_mutex on g_pool.mutex). */
void pool_lock(void);
void pool_unlock(void);

/* Return a pointer to backend at index idx. Bounds-checked. */
backend_t *pool_get_backend(int idx);

#endif /* EDGEMESH_BACKEND_H */
