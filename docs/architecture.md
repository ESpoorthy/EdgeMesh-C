# EdgeMesh-C Architecture

**Specification:** 1.2 (MVP) · **Platform:** macOS · **Core:** C17, POSIX sockets and pthreads

## 1. Topology and process boundaries

```text
React + TypeScript (localhost:5173)
        │ JSON, exact-origin CORS
        ▼
C application process ── admin listener 127.0.0.1:9090
        │                    └── snapshots / policy updates / fixed latest benchmark
        ├── proxy listener 127.0.0.1:8080 ──► selector ──► fixed backend pool
        │                                                  ├── 127.0.0.1:9001
        │                                                  ├── 127.0.0.1:9002
        │                                                  └── 127.0.0.1:9003
        └── health worker ────────────────────────────────► pool

C benchmark process ──► proxy ──► JSON/CSV under results/
```

The proxy and admin API are separate IPv4-loopback listeners in one C process. Three demo servers, the C benchmark and the dashboard are separate local processes. Backends identify themselves using `X-Backend-ID: backend-1|backend-2|backend-3` and expose `/health` for probes. No component accepts arbitrary destination URLs.

## 2. Module boundaries

| Module | Owns |
|---|---|
| `src/proxy` | Proxy listener, bounded accept/worker lifecycle, client response and top-level request accounting |
| `src/http` | Limited request/response parsing, header validation/filtering, status/error serialization and bounded I/O helpers |
| `src/backend` | Fixed backend records, policy state, health state snapshots and atomic selection/reservation |
| `src/health` | Periodic per-backend probes, thresholds, timestamps and health transitions |
| `src/metrics` | Synchronized counters, active counts, latency samples, rolling windows and snapshots |
| `src/admin_api` | Admin listener, route/method/query/body validation, CORS and JSON serialization |
| `src/common` | Monotonic/wall-clock conversion, logging, socket deadlines, configuration and safe helpers |
| `src/demo_backend` | C demo server, backend identity response and health endpoint |
| `benchmark` | C load generator, bounded workload options, statistics and actual JSON/CSV writer |
| `dashboard` | TypeScript types/API client and Overview, Backend Health, Benchmark Lab presentation |
| `tests` | Unit, API, end-to-end and failure/recovery harnesses |

Exact file names may change. These responsibilities and external contracts do not. Keep all network I/O outside state locks.

## 3. Runtime and proxy lifecycle

Use bounded pthread workers or an equivalently bounded design. Proxy and admin handling, health probes and shutdown must not wait indefinitely on each other. Defaults: at most 128 concurrent proxy connections, 8 admin connections and listen backlog 64 on each listener. When at capacity, close newly accepted sockets without parsing or responding. Client header read deadline is 2 seconds; admin header/body read deadline and response write deadline are each 2 seconds. Each upstream candidate has one total 2-second deadline covering connect, request write, response header/body read and client response write. The entire accepted proxy request has a 9-second deadline from accept, including request parsing, all attempts and generated error response; at most 1 second is reserved for the final 502/503/error write. Health probe has a 250 ms total deadline. Request header limit: 16 KiB; request line/target limit: 8 KiB; upstream status/header limit: 16 KiB; response body limit: 8 MiB. The benchmark accepts 1–100,000 requests and concurrency 1–128, with a 10-second total connect/write/read deadline per request. One request per client connection; send `Connection: close` and close.

Each timeout is a deadline, not a fresh timeout for each read/write call: recompute remaining time from a monotonic clock after partial I/O. A request can attempt at most three distinct backends, each at most once, while the overall 9-second deadline remains; preserve at least 1 second for the final error response. Retry only a connection, write, read, or protocol failure that occurs before any upstream response byte has been written to the client. Once response bytes are sent, never retry; finish the current stream or close on failure. A failure after response bytes start cannot be replaced by an HTTP error response.

Proxy client status mapping is fixed: malformed line/header/Host/invalid framing `400`; unsupported method `405` with `Allow: GET` (checked before body framing); valid but unsupported HTTP version `505`; request line or target over 8 KiB `414`; headers over 16 KiB `431`; GET body framing (`Transfer-Encoding` or nonzero `Content-Length`) `413`; incomplete headers at 2 seconds `408`; no healthy backend `503`; exhausted upstream attempts or invalid/incomplete upstream response before output `502`. At connection capacity, close without a response. Generated errors have `Content-Type: text/plain; charset=utf-8`, fixed short text bodies, `Content-Length`, and `Connection: close`; never reflect input. The admin API instead uses its documented JSON error envelope and HTTP status mapping.

Proxy sequence:

1. Accept and count a client connection under the connection cap. If the cap is already reached, close the accepted socket without reading or responding.
2. Read through `\r\n\r\n` with 2-second deadline and 16 KiB bound. Parse/validate only `GET /path[?query] HTTP/1.1`; require one Host; reject body framing and ambiguous headers. Apply the status mapping above.
3. Obtain eligible backend and reserve its in-flight slot atomically under the backend lock. If none is healthy, count a completed proxy failure and return 503.
4. Connect to the fixed backend; send a sanitized request with rewritten Host and `Connection: close`.
5. Parse bounded valid response headers and body framing before sending any upstream response bytes. Reject declared Content-Length above 8 MiB before forwarding headers; if a close-delimited body exceeds 8 MiB after streaming starts, close without retry. Responses accept a final 200–599 status on HTTP/1.0 or HTTP/1.1, one Content-Length or close-delimited body; informational 1xx and `Transfer-Encoding` are unsupported. Remove hop-by-hop response headers, emit `Connection: close`, and forward bounded bytes with partial-write handling. Do not forward a response with conflicting/duplicate Content-Length, invalid status/headers, or unsupported framing.
6. If an upstream operation fails before any upstream response byte is written to the client, release the reservation and try each remaining currently healthy backend at most once, subject to the per-attempt and overall deadlines. If attempts fail, send 502 within the overall deadline. If no backend is healthy at initial selection, return 503. Once any response byte is written, do not retry or send a second response; close on incomplete transfer.
7. On all paths release reservation and connection capacity once, record the terminal outcome and latency when applicable, close sockets and log a structured non-sensitive event. A client disconnect is a separate outcome, excluded from request totals and proxy/backend failure totals, and has no latency sample.

A downstream write error is a client disconnect: stop upstream work, count no backend attempt failure, and count only bytes actually written. A valid upstream 4xx/5xx is a successful forwarding outcome. The proxy does not guarantee end-to-end integrity once bytes have begun streaming.

## 4. Backend selection and health

Backend record fields: stable ID, fixed loopback socket address, `healthy|unhealthy|unknown` state, consecutive success/failure counters, last completed probe timestamp, optional probe latency, active proxy request count and per-backend attempt/success/failure counters.

Round robin starts after the last chosen slot and scans eligible entries. Least connections chooses the smallest active count; a rotating tie cursor chooses among equal minima. Selection plus reservation is atomic. Reservation release is mandatory on every success/error branch.

Health worker probes all three backends every 1 second, with a 250 ms total deadline, using `GET /health`; only a complete 2xx response with at most 1 KiB headers and 1 KiB body qualifies. Demo backends return status 200 and body `ok` for this route. Proxy request failures do not change backend health directly; only the consecutive probe thresholds do. Initial state is unknown. Three consecutive failures mark unhealthy; two consecutive successes mark healthy. Reset the opposite consecutive count after each result. Probe outside locks; apply result and timestamp under a short lock. Probe settings are fixed MVP defaults, not writable through the admin API. Probe requests and their bytes, latencies, errors, and responses are excluded from all proxy traffic metrics; probe state and latest probe time/latency are exposed only in backend health data.

## 5. Metrics model

Use a monotonic clock for deadlines/durations and wall-clock UTC for RFC 3339 timestamps. Store timestamped valid request outcomes, client rejections/disconnects, actual client response bytes and latency in bounded rings. Retain at most 3,600 closed UTC-aligned one-second buckets and 100,000 latency samples. Do not create a bucket until its UTC second has closed. A bucket is included in `/metrics` only if it contains a valid proxy outcome, client rejection or client disconnect and the closed bucket is fully within the requested interval. A point timestamp is its UTC bucket start; it covers `[timestamp, timestamp + 1 second)`. Each point contains cumulative counter snapshots at bucket end, rates equal that bucket's valid-request/response-byte deltas divided by exactly 1 second, p50/p95/p99 by nearest-rank, and `latency_samples` count. Do not pad missing buckets.

The overview uses a 60-second rolling interval `[now-60s, now)`; `/metrics` uses its requested interval. For rolling rates, divide the sum of events/bytes in the interval by `min(window_seconds, process_uptime_seconds)`; empty seconds count as zero. Rates are zero when there are no events. Percentiles use retained request latency samples whose timestamps fall in the interval; if none, return `null`. If the bounded sample ring overwrites older samples, the reported `latency_samples` count is the actual population used. Empty rolling windows report rates of zero and percentiles as null; the denominator is `min(window_seconds, process_uptime_seconds)`, so idle time is included. Response bytes are the bytes successfully written to client sockets, including proxy-generated errors and partial upstream responses. A partially forwarded invalid response is not a forwarded response.

Keep metric semantics exactly as in `requirements.md` and `api-contract.md`: rejected input and client disconnects are separate from valid request totals and proxy failures; a final valid request failure counts once despite retries; backend attempt failures include retry attempts; probe traffic is excluded; upstream HTTP error status is still a successfully forwarded response. `active_connections` counts admitted proxy client sockets, including header parsing; `active_requests` counts only parsed valid requests. Neither includes admin or health checks. `requests_total` counts valid requests with a proxy result and excludes input rejection and client disconnect. `responses_forwarded_total` increments only when a complete valid upstream response was fully written. Partial bytes still count toward `response_bytes_total` and the response byte rate. A failed backend attempt increments its `attempt_failures_total`. Client disconnects are counted separately and do not create proxy latency samples.

## 6. Administrative API and benchmark storage

The admin listener binds only `127.0.0.1:9090`, accepts only documented routes, one request per connection, maximum 16 KiB headers and 4 KiB bodies, and validates all method/path/query/content type/JSON fields. Only policy is mutable; only its two enum values are accepted. CORS exact origins are `http://localhost:5173` and `http://127.0.0.1:5173`; valid allowed preflight is 204 with route-specific methods (`GET, OPTIONS` for reads; `PUT, OPTIONS` for policy) and header `Content-Type`; there is no wildcard origin. CORS does not authenticate local callers.

The daemon and benchmark require the same absolute `--results-dir` startup argument. Reject a missing or relative path; canonicalize the directory once at startup and use only its `latest.json` child. The directory must exist and be accessible, or startup fails. This makes storage independent of working directory. Run one benchmark at a time. The benchmark atomically replaces `latest.json` after all requested tasks reach terminal outcomes, even if some failed; an interrupted run does not replace it. The API reads only this configured file, with a 64 KiB maximum and strict schema validation. Missing file => HTTP 200 with `available:false`; invalid/unreadable file => safe 500 and internal log. Never accept a path from a request or include the absolute path in API output. The API's `result_file` is the constant display name `latest.json`.

## 7. Dashboard contract

Dashboard uses only API paths/types in `api-contract.md`; C owns routing and proxy state. Poll no faster than every 2 seconds, cancel pending requests and timers on unmount, and expose loading, disconnected, error, empty and backend-health states. Display null percentiles as unavailable. Sample data is permitted only in a clearly labeled design or isolated test fixture, never as integrated live data.

## 8. Target repository layout

```text
AGENTS.md                 CMakeLists.txt              README.md
LICENSE                   docs/{requirements,architecture,api-contract,test-plan}.md
include/                  src/{proxy,http,backend,health,metrics,admin_api,common,demo_backend}/
tests/                    benchmark/                  dashboard/
results/                  (optional local run-data directory; configure an absolute path)
```

Create directories when their implementation is added; empty directories are not progress. Preserve the MIT `LICENSE` as-is.

## 9. Trade-offs

The fixed three-backend pool and one-request connections limit generality but keep destination control and parsing auditable. A small thread-based model is easier to demonstrate than an event loop within the deadline, but requires strict connection limits and lock discipline. Close-delimited response support permits simple demo servers; request bodies and chunked transfer are excluded to avoid parser complexity. Admin loopback binding reduces network exposure but is not authentication against local processes.
