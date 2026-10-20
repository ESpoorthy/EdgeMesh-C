# EdgeMesh-C Requirements

**Specification:** 1.2 (MVP) · **Target:** macOS · **Core:** C17/POSIX sockets · **Build/test:** CMake/CTest

## 1. Goal and scope

EdgeMesh-C is an educational C-first HTTP reverse proxy and load balancer. It routes a deliberately small HTTP/1.1 request subset across three local demonstration backends, removes failed backends from service, measures real traffic, and exposes a local JSON API consumed by a separate React + TypeScript dashboard. A repeatable C benchmark client records actual runs.

This is a demonstrable prototype, not a general purpose or production proxy. The target schedule is approximately 12 days; finish a reliable narrow slice before adding polish.

**Included:** proxy on `127.0.0.1:8080`; admin API on `127.0.0.1:9090`; three local backends on ports 9001–9003; round-robin and least-connections; active health checking and failover; counters, latency and bounded time-series data; C benchmark client; dashboard; unit, integration and backend-failure tests.

**Excluded:** TLS termination, HTTP/2, HTTP/3, WebSockets, CONNECT, upgrades, request bodies, keep-alive/persistent client connections, arbitrary backend registration or forwarding, authentication infrastructure, Kubernetes, cloud deployment, distributed operation and AI features.

## 2. Fixed topology

| Process/component | Bind/target | Responsibility |
|---|---|---|
| C proxy listener | `127.0.0.1:8080` | Client requests |
| C administrative listener | `127.0.0.1:9090` | Read-only status/metrics and policy update |
| Demo backend 1 | `127.0.0.1:9001` | Identifiable deterministic response with `X-Backend-ID: backend-1` |
| Demo backend 2 | `127.0.0.1:9002` | Identifiable deterministic response with `X-Backend-ID: backend-2` |
| Demo backend 3 | `127.0.0.1:9003` | Identifiable deterministic response with `X-Backend-ID: backend-3` |
| Dashboard dev server | `http://localhost:5173` or `http://127.0.0.1:5173` | API client and presentation |

One C application process owns both listeners. Each demo backend returns its ID in the response header `X-Backend-ID` and body for normal GETs; `GET /health` returns a complete 200 response with body `ok`. The backends, benchmark client and dashboard are separate processes. Backend targets are fixed local configuration; an incoming request can never select its own destination.

## 3. Functional requirements

### FR-01 — Limited HTTP proxy

- Parse one request per TCP connection. Accept only `GET`, origin-form targets beginning with `/`, and HTTP/1.1. A target must not contain a fragment, control characters, or an absolute authority. Preserve its path and query exactly.
- Require exactly one syntactically valid `Host` header. Reject request bodies: reject any `Transfer-Encoding`; accept `Content-Length` only when absent or exactly `0`. Reject duplicate/conflicting `Host` or `Content-Length`.
- Maximum complete request header section is 16 KiB, including CRLF delimiters. Header names follow HTTP token syntax; values may not contain bare control characters. Bound individual request-line length to 8 KiB.
- Require `Connection: close` semantics from the proxy: it always closes the client connection after one response. Forward only the request line and safe end-to-end headers; remove hop-by-hop headers and do not forward client `Connection` or `Proxy-Connection`. Add/replace `Host` with the configured backend authority and send `Connection: close` upstream.
- Apply bounded connect/read/write timeouts (defaults in architecture). Handle partial reads/writes. Never hold a shared-state lock across I/O.
- Return the exact status mapping below. Client input rejection is separate from proxy failure accounting.
- Responses are closed after one message. Parse a bounded status/header section; accept a final status from 200 through 599 on HTTP/1.0 or HTTP/1.1, and either one valid `Content-Length` or a close-delimited body. Reject informational 1xx responses, `Transfer-Encoding`, duplicate/conflicting lengths and malformed responses in the MVP. Forward the status and end-to-end headers/body, excluding hop-by-hop headers, and enforce configured header/body/time limits. An upstream failure yields 502 if no upstream response bytes reached the client; after any response bytes were sent, close the connection and record a failed request without attempting another backend. An incomplete upstream response counts as a failed backend attempt and final proxy failure; partial bytes actually written still count in `response_bytes_total`. A downstream write failure counts only as a client disconnect, not as a backend attempt failure.

| Condition | Proxy response/action |
|---|---|
| Malformed request line, header syntax, Host, or invalid/ambiguous framing | 400 Bad Request |
| Valid method other than GET | 405 Method Not Allowed, `Allow: GET` (method check takes precedence over body framing) |
| Valid request line with HTTP version other than HTTP/1.1 | 505 HTTP Version Not Supported |
| Request target/request line exceeds 8 KiB (checked before aggregate header size) | 414 URI Too Long |
| Header section exceeds 16 KiB after request-line check | 431 Request Header Fields Too Large |
| GET request with body framing (`Transfer-Encoding` or nonzero `Content-Length`) | 413 Content Too Large |
| Client does not finish request headers within 2 seconds | 408 Request Timeout |
| No healthy backend at selection time | 503 Service Unavailable |
| All upstream attempts fail before any response byte is sent to the client | 502 Bad Gateway; if a response already started, close the connection and do not send a second response |
| Proxy is at its connection cap | Close the newly accepted socket without a response |

Proxy-generated error responses have `Content-Type: text/plain; charset=utf-8`, a small fixed body, `Content-Length`, `Connection: close`, and no reflected request data. Error serialization must fit within the request deadline.

This is a small, documented subset, not full HTTP/1.1 compliance.

### FR-02 — Policies

- Support exactly `round_robin` and `least_connections`.
- Both consider healthy backends only. Round robin advances from the last selected slot and skips unhealthy entries.
- Least-connections selects the backend with the fewest in-flight proxy requests. Ties use a rotating cursor for deterministic fair selection.
- Selection and increment of the chosen backend's in-flight reservation are atomic under shared-state synchronization. Release the reservation exactly once on every exit path.
- On an upstream connect/write/read/protocol failure, mark that backend attempt failed and retry another currently healthy backend only if zero upstream response bytes have been sent to the client. Attempt each backend at most once, for at most three total candidates. Each attempt has a 2-second total deadline and the whole accepted request has a 9-second deadline from accept, with at least 1 second reserved for the final error response. A valid upstream HTTP error response (4xx/5xx) is forwarded and is not a proxy failure. Proxy-attempt failures are counted but only health-probe thresholds change backend health state.

### FR-03 — Health and failover

- A worker probes each configured backend independently every 1 second with a 250 ms total probe deadline, requesting `GET /health` and accepting only a complete 2xx response with at most 1 KiB of headers and 1 KiB of body.
- Initially state is `unknown`. Three consecutive failures transition to `unhealthy`; two consecutive successes transition to `healthy`. Reset the opposing counter on each result. Only healthy backends receive new work.
- Publish the last completed probe timestamp and latency. Do not perform network I/O while holding backend-state locks.
- Within the default settings, a stopped backend should leave selection within 3.5 seconds (three intervals plus probe timeout); a recovered backend should re-enter within 2.5 seconds (two intervals plus timeout). Tests allow scheduler tolerance up to 5 seconds and 4 seconds respectively.

### FR-04 — Measured metrics

- Record completed proxy requests, forwarded upstream responses, proxy failures, rejected client requests separately, current admitted proxy client connections, current valid active requests, per-backend counts, response bytes, latency samples and bounded time series.
- `active_connections` is the current number of admitted proxy client TCP connections, including clients whose headers are still being parsed; it is decremented on every close path. `active_requests` counts valid parsed requests still being processed, including ones ending in client disconnect. Neither includes admin or health-check connections.
- `requests_total` counts valid parsed requests that reach a proxy result (complete upstream response, 502 or 503), whether forwarded or failed. `client_rejections_total` counts malformed, unsupported, oversized, and timed-out request input. `client_disconnects_total` counts valid clients disconnecting before the proxy completes its response; disconnects are excluded from `requests_total` and `proxy_failures_total`. `responses_forwarded_total` counts complete valid upstream responses fully written to the client, including upstream 4xx/5xx. `proxy_failures_total` counts final failed valid requests (no healthy backend or upstream failure after retry); it increments once per request, not per attempt.
- A backend's `requests_total` counts attempts made to it; its `responses_forwarded_total` counts complete valid upstream responses fully written through it; its `attempt_failures_total` counts failed attempts, including an incomplete upstream response. Across retries, backend attempt totals may exceed process request totals.
- Probe traffic is excluded from all user-facing request, response, failure, byte, latency, and rate metrics. Probe state, timestamp and latency appear only in backend health fields.
- Request latency begins after request headers are fully parsed and ends when the response is fully forwarded or the valid request reaches a terminal 502/503 failure. Client-input rejection and client disconnect have no proxy latency sample. A response that begins but is incomplete contributes a failure latency sample.
- Overview rolling rates use a 60-second window; `/metrics` uses its requested 10–300 second window. Divide window totals by `min(window_seconds, process_uptime_seconds)`; idle seconds count as zero traffic. Thus no observations yield a rate of 0 and percentile `null`.
- The series consists of closed, UTC-aligned one-second buckets. Emit only buckets with valid proxy outcomes, client rejections or disconnects. Each bucket rate is its valid request/response byte delta divided by 1.0 second; no missing bucket is fabricated. Its cumulative counters are snapshots at bucket end. Percentiles use nearest-rank over latency samples in that bucket. The rolling overview percentiles use samples in the rolling window.
- `response_bytes_total` counts all response bytes successfully written to proxy clients, including proxy-generated errors and partial upstream responses; it includes status line, headers and body. An incomplete upstream response does not increment `responses_forwarded_total`, even if some bytes reached the client. `response_bytes_per_second` uses the same byte total and window denominator above.
- Retain at most 3,600 one-second buckets and 100,000 timestamped latency samples in fixed-size rings. Percentiles use retained samples in the requested interval; expose the sample count so consumers can see the population used. JSON counters saturate at `9,007,199,254,740,991` (the largest exactly representable JavaScript integer).
- All accepted valid proxy requests receive exactly one terminal outcome. A client disconnect is counted separately, excluded from `requests_total` and `proxy_failures_total`, and is not also a client rejection. A rejected request is not counted in `requests_total`.

### FR-05 — Admin JSON API

Implement exactly the routes and schemas in `api-contract.md`. The API is local-only, validates request method/path/query/body and enum values, returns the shared error envelope, and never accepts arbitrary upstream URLs, file paths or commands. Each response closes its connection.

### FR-06 — Benchmark client

Provide a C CLI targeting only `http://127.0.0.1:8080/` with an optional origin-form path. Accept 1–100,000 requests and concurrency 1–128; reject invalid values. Each benchmark request has a 10-second total connect/write/read deadline, longer than the proxy request deadline. A completed benchmark request is a complete parseable HTTP response with status 200–499; failed includes transport error, malformed/incomplete response or status 500–599. Report requested, completed, failed, wall-clock duration from first dispatch until all requests are terminal, RPS (`completed / duration`), response bytes/s (`bytes read / duration`), p50/p95/p99 for completed requests and backend distribution from the recognized `X-Backend-ID` response header (responses without a known ID are not attributed to a backend). `response_bytes` is the number of response bytes actually read, including partial failed responses. Save actual completed run data as JSON. A run is complete once every requested task has a terminal result, even if some requests failed. The daemon and benchmark require the same absolute `--results-dir` startup setting; the admin API reads only `<results-dir>/latest.json` and never takes a caller-supplied path. Never invent values; unavailable percentiles are `null`.

### FR-07 — Dashboard

A separate React + TypeScript dashboard consumes only the defined admin API for live data. It includes Overview, Backend Health and Benchmark Lab views; allows policy changes; shows loading, disconnected/error, empty and unhealthy states; labels unavailable data; and polls at intervals no faster than once every 2 seconds. No implementation or design prototype may present mock values as live.

## 4. Non-functional and security requirements

- C17 for proxy, API, demo servers and benchmark; POSIX/BSD sockets and pthreads on macOS; CMake/CTest. Use strong warnings and sanitizers where available.
- Check system calls and allocations; handle partial I/O, EINTR, disconnects, timeouts, shutdown and cleanup. Bound parsing, bodies, buffers, worker/connection counts, history and benchmark parameters.
- Synchronize policy, backend health, reservations and metrics. Avoid global locks during network operations. Escape JSON strings correctly.
- Capacity defaults: at most 128 concurrent proxy client connections, 8 concurrent admin connections and listen backlog 64 per listener. Close newly accepted sockets promptly when at capacity without parsing them or sending a response. These are configuration constants, not dashboard controls.
- Both listeners bind explicitly to IPv4 loopback (`127.0.0.1`), never wildcard or LAN interfaces. Backend connection targets are a compile-time/default fixed three-entry loopback pool; if ports are made configurable for tests, accept only validated loopback addresses and reject any non-loopback host.
- Admin body maximum 4 KiB; headers maximum 16 KiB. CORS allows only the two dashboard origins in the topology, reflects the exact allowed origin with `Vary: Origin`, and never uses `*`. CORS is not authentication; any local process may call loopback. Do not expose port 9090 externally or claim production security.
- Do not read caller-selected files, execute commands, fetch arbitrary URLs, or log sensitive arbitrary headers. At startup the daemon and benchmark receive the same required absolute `--results-dir`; canonicalize it once and use only its `latest.json` child. The directory must exist and be accessible; otherwise startup fails. The API accepts no path. Validate the file schema and 64 KiB maximum size.

## 5. Measurable acceptance criteria

1. A clean macOS checkout builds with the documented CMake/C17 workflow and CTest discovers the documented test suites.
2. A valid GET through port 8080 returns a valid identifiable demo-backend response; malformed syntax/Host/invalid framing => 400, unsupported method => 405 plus `Allow: GET`, unsupported version => 505, target/request line >8 KiB => 414, headers >16 KiB => 431, GET body indicators => 413, and incomplete headers at 2 seconds => 408.
3. Sequential equal-backend round-robin requests distribute counts within one request; unhealthy entries are skipped.
4. Least-connections chooses the lowest live in-flight count; ties rotate deterministically in controlled tests.
5. A failed backend stops receiving new selections within the specified probe bound; remaining backends serve traffic; recovery returns it after threshold.
6. All unhealthy backends => HTTP 503. Upstream attempts have 2-second total deadlines, retry each other eligible backend at most once only before response bytes reach the client, and stay within the 9-second accepted-request deadline; exhausted failures => 502 if no response started.
7. All six API routes match the schemas and error rules in `api-contract.md`; invalid policy leaves state unchanged.
8. Real successful, failed, rejected and disconnected requests change only their specified counters; health probes do not affect traffic metrics; no-data percentile values are null; active reservations return to zero after completion/failure/disconnect.
9. Benchmark output contains actual run configuration and counts; reported rates/percentiles can be recomputed from the saved raw/summary data or are explicitly unavailable.
10. Dashboard live values come from the API and its empty/disconnected states do not imply fabricated values.
11. Unit, integration, backend stop/restart and all-backends-down tests pass. Evidence must identify actual commands/results; writing this spec is not evidence of a pass.
12. The daemon and benchmark load/write only `<absolute --results-dir>/latest.json`, even when launched from a different working directory; the API never accepts a file path.

## 6. Deadline priorities

Days 1–2: contracts and build skeleton. Days 3–5: bounded parser, proxy and demo backends. Days 6–7: selector, health and failover. Days 8–9: admin API and real metrics. Days 10–11: integration/failure tests, benchmark and minimal dashboard integration. Day 12: reproducible demo, defect fixes and evidence. If time slips, prioritize correctness, test coverage and documented limitations over extra UI polish or throughput optimization.
