# EdgeMesh-C Administrative API Contract

**Version:** 1.3 (MVP) · **Base URL:** `http://127.0.0.1:9090` · **Encoding:** UTF-8 JSON

Allowed browser origins: `http://localhost:5173`, `http://127.0.0.1:5173`. This contract is the source of truth for the dashboard, tests and C API.

## 1. Common rules

- Bind only to `127.0.0.1:9090`. One request per connection; respond with `Connection: close`.
- HTTP request headers max 16 KiB; bodies max 4 KiB. Header/body read deadline is 2 seconds; response write deadline is 2 seconds. Support HTTP/1.1, one request per connection and `Connection: close`. Request framing uses at most one valid `Content-Length`; reject `Transfer-Encoding`, duplicate/conflicting lengths and a body on GET with 400. A PUT without valid JSON body is 400. JSON responses use `Content-Type: application/json; charset=utf-8`.
- Timestamps are UTC RFC 3339 strings (`2026-10-19T05:30:00Z`) or `null` if unavailable. Durations in fields ending `_ms` are milliseconds; rates ending `_per_second` are per second. Counts are non-negative JSON integers saturated at `9007199254740991` so TypeScript can represent them exactly. No invented samples.
- Unknown route => 404; wrong method => 405 with `Allow`; malformed request/header/JSON/value/query => 400; header section over 16 KiB => 431; body over 4 KiB => 413; unsupported media type => 415; incomplete headers/body after 2 seconds => 408; internal error => 500. Every error uses the envelope below. No stack traces or sensitive local paths in responses.
- CORS returns only the exact matching allowed Origin plus `Vary: Origin`; other origins receive no allow-origin header. For allowed origins, valid OPTIONS preflight returns 204 with route-specific `Access-Control-Allow-Methods` and `Access-Control-Allow-Headers: Content-Type`. Never wildcard. CORS is not authentication.
- No endpoint accepts arbitrary backend addresses, URLs, filesystem paths or commands. These are the only six application routes; OPTIONS is CORS preflight only. Query strings are accepted only on `/api/v1/metrics`; all other routes reject queries with 400.

| Path | Allowed application method | `Allow` on 405 |
|---|---|---|
| `/api/v1/health` | GET | `GET, OPTIONS` |
| `/api/v1/overview` | GET | `GET, OPTIONS` |
| `/api/v1/backends` | GET | `GET, OPTIONS` |
| `/api/v1/policy` | PUT | `PUT, OPTIONS` |
| `/api/v1/metrics` | GET | `GET, OPTIONS` |
| `/api/v1/benchmarks/latest` | GET | `GET, OPTIONS` |

OPTIONS is preflight only; it does not add an API data route. For an allowed Origin, valid preflight returns 204 and route-specific `Access-Control-Allow-Methods` (`GET, OPTIONS` for read routes; `PUT, OPTIONS` for policy), `Access-Control-Allow-Headers: Content-Type`, the exact `Access-Control-Allow-Origin`, and `Vary: Origin`. For other origins, omit allow-origin. OPTIONS without a valid preflight request is 400; reject a requested method not allowed for that route or requested headers other than `Content-Type`. An unsupported route is 404 even for OPTIONS.

### Error response

```json
{"error":{"code":"INVALID_POLICY","message":"policy must be round_robin or least_connections"}}
```

`code` is a stable uppercase identifier, `message` is brief and safe. Codes: `NOT_FOUND`, `METHOD_NOT_ALLOWED`, `BAD_REQUEST`, `INVALID_POLICY`, `PAYLOAD_TOO_LARGE`, `HEADERS_TOO_LARGE`, `REQUEST_TIMEOUT`, `UNSUPPORTED_MEDIA_TYPE`, `INTERNAL_ERROR`.

## 2. `GET /api/v1/health`

HTTP 200 with `status: "ok"` indicates the administrative API is responding. It does not assert proxy listener readiness or backend health. Dashboard administrative connectivity is its own client-observed state based on actual API requests; it is not returned by this endpoint.

```json
{"status":"ok","service":"edgemesh-c","version":"0.1.0","uptime_seconds":128}
```

## 3. `GET /api/v1/overview`

HTTP 200 snapshot. Aggregate metric window is always 60 seconds.

```json
{
  "proxy_running": true,
  "policy": "round_robin",
  "uptime_seconds": 128,
  "backends": {"total":3,"healthy":3,"unhealthy":0,"unknown":0},
  "metrics": {
    "requests_total":420,
    "responses_forwarded_total":414,
    "proxy_failures_total":6,
    "client_rejections_total":2,
    "client_disconnects_total":1,
    "response_bytes_total":1094400,
    "requests_per_second":7.2,
    "response_bytes_per_second":18240,
    "active_connections":3,
    "active_requests":2,
    "latency_samples":420,
    "latency_p50_ms":4.8,
    "latency_p95_ms":12.6,
    "latency_p99_ms":18.1
  },
  "sample_window_seconds":60,
  "generated_at":"2026-10-19T05:30:00Z"
}
```

`requests_total`, `responses_forwarded_total`, `proxy_failures_total`, `client_rejections_total`, `client_disconnects_total` and `response_bytes_total` are process-lifetime cumulative counters. `active_connections` is the current number of admitted proxy client sockets, including header parsing; `active_requests` is the current number of valid parsed proxy requests in progress. Neither includes admin or health-check sockets. `latency_samples` is the number retained in the 60-second interval and used for percentiles. Rates use the rolling window and its specified denominator. No samples => percentile `null`; no traffic => rates are zero.

`proxy_running` is true only while the proxy listener is initialized and accepting connections; it is independent of admin API health and backend health. The UI must not infer the dashboard's admin connectivity from this flag.

## 4. `GET /api/v1/backends`

HTTP 200; includes all three configured backends in stable ID order.

```json
{
  "backends":[
    {"id":"backend-1","address":"127.0.0.1:9001","state":"healthy",
     "active_connections":1,"requests_total":180,"responses_forwarded_total":178,
     "attempt_failures_total":2,"attempts_per_second":3.0,"traffic_percentage":33.3,
     "last_check_at":"2026-10-19T05:29:59Z","last_probe_ms":1.7}
  ],
  "traffic_window_seconds":60,
  "generated_at":"2026-10-19T05:30:00Z"
}
```

The example shows one entry; a real response has exactly three in stable ID order. State is exactly `healthy`, `unhealthy` or `unknown`; the server does not return `Recovered`, recovery history or recovery timestamps. Counts are lifetime counts; last probe fields (`last_check_at`, `last_probe_ms`) are `null` before a probe completes. `requests_total` counts dispatched attempts to that backend, including retries; `attempt_failures_total` counts failed/incomplete upstream exchanges against that backend. Probe traffic is excluded.

The response-level `traffic_window_seconds` is always `60` seconds (maximum observation window). Per backend, `attempts_per_second` is a JSON number measured in attempts/second: the actual dispatched-attempt delta in the UTC-aligned one-second buckets intersecting `[generated_at - 60 seconds, generated_at)`, including the current in-progress bucket, divided by `min(60, process_uptime_seconds)`. It measures actual user-request backend attempts, including failed attempts/retries; a request returning 503 before dispatch adds no backend attempt. One-second bucket resolution applies. `traffic_percentage` is a JSON number from 0 through 100, or `null` when the denominator is zero; it is the backend's same-window attempt delta divided by the sum of all three backend attempt deltas in the same window, multiplied by 100. If that sum is zero, `attempts_per_second` is `0.0` and `traffic_percentage` is `null` for every backend. Otherwise percentages total 100 before rounding (rounded to one decimal in JSON). Do not derive these values from lifetime counters or fixture data.

## 5. `PUT /api/v1/policy`

Require `Content-Type: application/json` (parameters such as charset are allowed). Exact request schema:

```json
{"policy":"least_connections"}
```

Allowed values: `round_robin`, `least_connections`. On success return 200:

```json
{"policy":"least_connections","updated":true}
```

Missing/extra fields, wrong JSON types, invalid enum, malformed/trailing JSON => 400. Oversized body => 413; wrong content type => 415. Rejection must leave current policy unchanged. A successful request returns the active policy after synchronized update.

## 6. `GET /api/v1/metrics?window_seconds=60`

Optional query integer, default 60, range inclusive 10–300. Reject duplicate, malformed, non-integer or out-of-range query parameters with 400. Unknown query keys also produce 400.

```json
{
  "window_seconds":60,
  "points":[
    {"timestamp":"2026-10-19T05:29:00Z","requests_total":390,
     "responses_forwarded_total":385,"proxy_failures_total":5,
     "client_rejections_total":2,"client_disconnects_total":1,"response_bytes_total":990000,
     "requests_per_second":6.4,"response_bytes_per_second":16500,
     "latency_samples":6,
     "latency_p50_ms":4.6,"latency_p95_ms":11.9,"latency_p99_ms":17.0}
  ],
  "generated_at":"2026-10-19T05:30:00Z"
}
```

`points` contains only real completed one-second UTC-aligned buckets fully contained in `[generated_at - window_seconds, generated_at)`, in ascending timestamp order; it may be empty immediately after startup. `timestamp` is the bucket start and the bucket covers `[timestamp, timestamp + 1 second)`. All `*_total` fields are cumulative snapshots at bucket end. Rates are event/byte deltas in that bucket divided by exactly 1 second. Idle buckets are omitted; do not pad gaps. `latency_samples` is the number of retained latency samples used for the bucket's nearest-rank percentiles. Percentiles with no samples are `null`. `requests_per_second` counts valid proxy requests; `response_bytes_per_second` includes written proxy errors and partial responses.

## 7. `GET /api/v1/benchmarks/latest`

Returns newest completed benchmark record; does not start a benchmark and accepts no query parameters. The client accepts 1–100,000 requests and concurrency 1–128, with a 10-second total connect/write/read deadline per request. The daemon and benchmark require the same absolute `--results-dir` at startup; it must exist and be accessible or startup fails. The server reads only `<results-dir>/latest.json`, independent of working directory, as specified in architecture. Run one benchmark at a time; after every requested task reaches a terminal outcome, atomically replace the file, including runs with failed requests. An interrupted run does not replace it.

No saved result (HTTP 200):

```json
{"available":false,"run":null}
```

Saved result (HTTP 200):

```json
{
  "available":true,
  "run":{
    "run_id":"bench-20261019-053000","completed_at":"2026-10-19T05:30:00Z",
    "policy":"round_robin","target":"http://127.0.0.1:8080/",
    "requested_requests":1000,"concurrency":20,"completed_requests":996,"failed_requests":4,
    "duration_seconds":8.4,"requests_per_second":118.6,"response_bytes_per_second":246000,
    "latency_p50_ms":92.0,"latency_p95_ms":180.0,"latency_p99_ms":240.0,
    "response_bytes":2066400,"backend_distribution":{"backend-1":334,"backend-2":331,"backend-3":331},
    "result_file":"latest.json"
  }
}
```

All numbers above are illustrative schema only. A real run records actual values. A completed request is a complete parseable HTTP response with status 200–499; a failed request is a transport error, incomplete/malformed response, or HTTP 5xx. A run is complete when all requested tasks have terminal outcomes, even if some failed. `completed_requests + failed_requests` must equal `requested_requests`; RPS is completed divided by wall-clock duration from first dispatch until the last terminal outcome. Percentiles use completed requests and are null if there are no completed latency samples. `response_bytes` counts bytes actually read from the proxy, including incomplete responses; backend distribution counts complete responses with a recognized `X-Backend-ID` header and omits unrecognized/missing IDs; response bytes/s is bytes read divided by run duration. `target` is restricted to the local proxy; `result_file` is the constant display basename, not a path. Invalid/unreadable saved data => safe 500, internal reason only in logs. Run one benchmark at a time; the client atomically updates `latest.json` only after a run completes.

## 8. Metric definitions and rounding

- `requests_total`: valid parsed proxy requests with a terminal proxy result (complete upstream response, 502, or 503); input rejections and client disconnects are excluded.
- `responses_forwarded_total`: complete valid upstream responses forwarded, including upstream 4xx/5xx.
- `proxy_failures_total`: final failed valid request outcomes (no healthy backend or unsuccessful upstream exchange after retries), incremented once per request.
- `client_rejections_total`: malformed, unsupported, oversized or timed-out client requests rejected before valid proxy processing. `client_disconnects_total` counts valid clients disconnecting before response completion; neither is also a proxy failure or a client rejection.
- Backend `requests_total` and `attempt_failures_total` count per-backend attempts/failures; one client request may attempt multiple backends.
- Health probes are excluded from all user-facing traffic and latency metrics.
- Latency starts after complete request headers are parsed and ends when a complete response is forwarded or a valid request reaches terminal 502/503 failure. Client input rejection/disconnect has no latency sample. Percentiles use nearest-rank and retained request latency samples in the specified interval; expose sample count; no samples => `null`.
- `response_bytes_total` includes proxy-generated errors and all upstream response bytes actually written to client sockets, including partial/incomplete responses; `responses_forwarded_total` increments only for a complete valid response fully written. `response_bytes_per_second` divides window bytes by `min(window_seconds, process_uptime_seconds)`; idle time counts as zero and no traffic yields zero rate.
- Rolling overview rates use the latest 60 seconds; metrics endpoint uses the requested interval. Bucket rates use the fixed one-second bucket duration. Percentiles are computed over samples in the respective interval, up to the 100,000-sample retention cap.
- JSON numeric durations/rates may be rounded to one decimal place; counters are exact integers.
- Per-backend `attempts_per_second` uses actual attempt deltas from the latest 60 UTC-aligned one-second buckets intersecting the interval, including the current bucket; rate denominator is `min(60, process_uptime_seconds)`. Traffic percentage uses the same attempt sums; zero total attempts produces `null`, otherwise the three percentages sum to 100 before rounding.

## 9. Dashboard behavior and contract tests

Implement exactly the three designed app screens/routes: Overview (`/`), Backend Health (`/backends`) and Benchmark Lab (`/benchmarks`). Design System and React + TypeScript Handoff are implementation references, not runtime routes. Preserve the approved shared dark navy/charcoal shell, compact sidebar, dense cards/charts, light text, cyan/teal emphasis, restrained amber status accents and Design System tokens. Use these data bindings: Overview uses `/health`, `/overview`, `/backends` and `/metrics?window_seconds=60`; Backend Health uses `/backends` plus the successful PUT response from `/policy`; Benchmark Lab uses `/benchmarks/latest` only. The six documented application endpoints remain the entire API surface; frontend paths are local navigation.

Represent admin API health (`/health`), proxy readiness (`proxy_running`), each backend's server state (`healthy|unhealthy|unknown`) and dashboard-to-admin connectivity (client-observed request state) independently. `Recovered` is not a server field/state; show it only in labeled prototype data or as a transient UI cue after the client observes an unhealthy-to-healthy transition. No server recovery history/timestamp exists. Keep fixtures visibly marked `PROTOTYPE DATA` and separate from live adapters; live screens use only response values. Benchmark Lab shows the latest saved result or the `available:false` empty state; it does not start runs or show run history. Include loading, stale, disconnected, error, empty/no-result, unhealthy and unknown states as applicable. Never render null values as measured zero.

Poll no faster than every two seconds. Treat `null`, empty points and `available:false` as unavailable/empty, not zero or fabricated data. Test success and failure for each route, methods, unknown paths/query keys, invalid and oversized bodies, CORS origin behavior, backend-rate formula/zero-traffic behavior, empty/populated benchmark files, schema errors, and consistency with TypeScript types.
