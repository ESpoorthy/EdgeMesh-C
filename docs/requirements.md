# EdgeMesh-C Requirements

**Version:** 1.0 — MVP specification  
**Target environment:** macOS  
**Primary language:** C17  
**Build/test:** CMake and CTest

## 1. Product goal

EdgeMesh-C is an educational, C-first HTTP reverse proxy and load balancer. It distributes requests across three configured local backends, checks backend health, avoids unhealthy backends, records real performance metrics and exposes a local JSON API for a React + TypeScript dashboard. A repeatable C benchmark client compares behaviour under defined workloads.

This is a course project prototype, not a production-grade general-purpose proxy.

## 2. Fixed topology

One C application process owns two separate listeners:

| Component | Listen address | Purpose |
|---|---|---|
| Proxy | 127.0.0.1:8080 | Client HTTP traffic |
| Admin API | 127.0.0.1:9090 | Dashboard metrics and policy updates |

Three separate demo backend instances listen at 127.0.0.1:9001, 127.0.0.1:9002 and 127.0.0.1:9003. Each backend returns a small response identifying itself. Backend addresses are fixed in local configuration; clients cannot provide arbitrary upstream destinations.

The dashboard development server is expected at http://localhost:5173 or http://127.0.0.1:5173.

## 3. Functional requirements

### FR-01 — HTTP proxy
- Support a deliberately limited HTTP/1.1 subset: origin-form GET requests, bounded headers, one request per client connection and Connection: close.
- Require a valid request line and valid header syntax; maximum request-header section is 16 KiB.
- Preserve the path and query string when forwarding to a selected configured backend.
- Use bounded connection/read timeouts and correctly handle partial reads and writes. See the Timeout and retry constants table in docs/architecture.md for exact values.
- Return clear errors for malformed, oversized or unsupported requests.
- Do not claim full HTTP/1.1 conformance. Request bodies, chunked request bodies, persistent connections, WebSockets, CONNECT, upgrades and arbitrary absolute URLs are out of scope.
- Maximum concurrent proxy connections is 128; a new connection beyond this limit receives HTTP 503 and is closed immediately.

### FR-02 — Load balancing
- Support exactly two MVP policies: round_robin and least_connections.
- Round robin rotates through eligible healthy backends.
- Least connections chooses the healthy backend with the fewest in-flight proxy requests; ties are resolved deterministically with rotation.
- Backend selection and reservation of an in-flight count must be thread-safe.
- If no backend is healthy, return HTTP 503 Service Unavailable.

### FR-03 — Health checking and failover
- Periodically probe all configured backends using bounded timeouts. See the Timeout and retry constants table in docs/architecture.md for exact values.
- Track healthy, unhealthy or unknown state, last-check timestamp and probe latency when available.
- Use documented consecutive-failure and consecutive-success thresholds to avoid flapping. The documented thresholds are 3 consecutive failures to mark unhealthy and 2 consecutive successes to recover; probe interval is 5 s.
- Exclude unhealthy backends from new selections and return recovered backends to service after the recovery threshold.
- Do not block the entire proxy while a probe is waiting on network I/O.

### FR-04 — Real metrics
Collect request totals, successfully forwarded responses, proxy failures, active requests, per-backend request counts, response bytes, request latency and a bounded time series.
- A successfully forwarded response is a valid upstream HTTP response received and forwarded; upstream 4xx/5xx responses are still forwarded responses.
- A proxy failure includes no healthy backend, connect/read timeout, or invalid/incomplete upstream response. Track rejected client input separately where practical. If upstream response headers are never successfully received, increment proxy_failures_total and do not increment responses_forwarded_total. If response forwarding begins but the upstream body drops mid-transfer, increment responses_forwarded_total and count only bytes actually written to the client.
- Proxy latency starts after request headers are parsed and ends when the upstream response has been forwarded.
- Define p50/p95/p99 over the documented rolling sample window. If no samples exist, return null, not a fabricated zero.
- requests_per_second and response_bytes_per_second return 0.0 (not null) when fewer than 2 rate buckets exist. Health-probe traffic does not contribute to proxy metrics.
- Bound metrics history and retained latency samples.

### FR-05 — Administrative JSON API
Implement the endpoints in api-contract.md and keep request/response schemas consistent with the frontend and tests. Use appropriate HTTP status codes and a common JSON error envelope. Maximum concurrent admin connections is 16; a new connection beyond this limit receives HTTP 503 and is closed immediately.

### FR-06 — Benchmark client
- Provide a C command-line load generator for the local proxy with bounded request-count and concurrency options.
- Record configuration, requested/completed/failed requests, wall-clock duration, requests per second, response bytes per second and p50/p95/p99 latency.
- Save actual completed-run results in JSON and/or CSV.
- Do not fabricate results; unavailable measurements must be represented explicitly.

### FR-07 — Dashboard
- Implement Overview, Backend Health and Benchmark Lab screens.
- Use API responses for live values. Sample values may be used only in the design prototype or isolated frontend tests and must not be presented as live data.
- Display loading, disconnected, errors, empty results and unhealthy-backend states.
- Allow policy changes through the API and display whether the operation succeeded.

### HTTP status codes

The following table defines the HTTP status code returned for each condition across both the proxy listener (127.0.0.1:8080) and the admin API listener (127.0.0.1:9090).

| Condition | HTTP Status |
|---|---|
| Malformed request line or headers | 400 Bad Request |
| Request headers exceed 16 KiB | 431 Request Header Fields Too Large |
| Unsupported method (non-GET) | 405 Method Not Allowed |
| No healthy backend available | 503 Service Unavailable |
| Backend connect timeout | 502 Bad Gateway |
| Backend read timeout (response headers) | 504 Gateway Timeout |
| Invalid or incomplete upstream response | 502 Bad Gateway |
| Max proxy connections exceeded | 503 Service Unavailable |
| Admin request body exceeds 4 KiB | 413 Content Too Large |
| Unknown admin route | 404 Not Found |
| Unsupported admin method | 405 Method Not Allowed |
| Admin malformed JSON or invalid value | 400 Bad Request |
| Admin internal error | 500 Internal Server Error |
| Max admin connections exceeded | 503 Service Unavailable |

## 4. Non-functional requirements

- C17 is the implementation language for proxy, admin API, demo backend and benchmark client. React + TypeScript is only the presentation layer.
- Use POSIX/BSD sockets and pthreads supported by macOS; isolate platform-specific behaviour.
- Use CMake and CTest. Enable strong compiler warnings and use sanitizers where supported.
- Validate lengths and values; check system-call results; handle partial I/O, disconnects and timeouts; avoid unbounded buffers and unchecked allocation.
- Synchronise backend state, policy, active-connection reservations and metrics. Do not hold global locks during network I/O.
- Log health transitions and errors without logging arbitrary sensitive headers.
- Document build, run, test and benchmark commands.

## 5. Local security boundaries

- Bind proxy and admin listeners to loopback only by default.
- Backend targets are fixed to the configured local pool; no open-proxy or arbitrary URL-fetching capability.
- Bound header sizes, admin body sizes, connections and metric history.
- Allow only the documented local dashboard origins for browser CORS and never use wildcard CORS for policy-changing routes.
- CORS is not authentication and loopback is not a boundary against other local processes running as the same user. Do not expose the admin API publicly or claim production-grade security.
- TLS termination, HTTP/2, authentication infrastructure, cloud deployment and Kubernetes are out of scope.

## 6. Acceptance criteria

1. A clean checkout builds on macOS using documented CMake commands.
2. A request through the proxy receives a valid response from a configured demo backend.
3. Both policies can be selected; invalid values are rejected without changing the current policy.
4. Round robin rotates across healthy backends under a sequential test workload.
5. Least connections uses tracked in-flight counts with deterministic tie-breaking.
6. Stopping a backend marks it unhealthy within the configured probe threshold; remaining healthy backends continue serving requests.
7. If all backends are unavailable, a valid HTTP 503 is returned.
8. All six documented admin endpoints return schema-conforming JSON and documented errors.
9. Counters and latency metrics change in response to real requests.
10. The benchmark client writes actual results and their configuration to a result file.
11. Tests cover malformed requests, policy changes, failure/recovery and the no-healthy-backend path.
12. The dashboard reads live API data and handles API disconnection and missing results.

Record commands and results as evidence. Source code existing is not proof that a criterion passed.

## 7. Explicitly deferred

TLS, HTTP/2, persistent connections, request-body forwarding, WebSockets, dynamic arbitrary backend registration, distributed operation, production authentication, autoscaling, deployment infrastructure and complex animation.
