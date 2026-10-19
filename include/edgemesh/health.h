#ifndef EDGEMESH_HEALTH_H
#define EDGEMESH_HEALTH_H

#include "edgemesh/backend.h"

/* Health-check threshold constants (from architecture.md §4). */
#define HEALTH_FAIL_THRESHOLD   3   /* consecutive failures → UNHEALTHY */
#define HEALTH_PASS_THRESHOLD   2   /* consecutive successes → HEALTHY  */
#define HEALTH_PROBE_INTERVAL_S 5   /* seconds between probe rounds     */

/*
 * health_worker_start(pool)
 *
 * Spawns a pthread that probes each backend every HEALTH_PROBE_INTERVAL_S
 * seconds.  Updates backend state, last_check_at, last_response_ms under the
 * pool lock.  Network I/O is performed without the pool lock held.
 */
void health_worker_start(backend_pool_t *pool);

/*
 * health_worker_stop()
 *
 * Sets a stop flag and joins the health-check pthread.
 */
void health_worker_stop(void);

#endif /* EDGEMESH_HEALTH_H */
