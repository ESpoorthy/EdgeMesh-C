# M3 teammate handoff — Admin API and real metrics

**Start only after M2 PR #3 is merged into `main`.** Use the current `main` HEAD as the base, create a separate `milestone3/admin-api-metrics` branch, and do not commit directly to `main`.

M1 and M2 provide a C17 proxy, a three-backend pool, Round Robin and Least Connections selection, bounded proxy concurrency, health probes, recovery, and retry before downstream response bytes are sent. Read `AGENTS.md` plus the following authoritative contracts before coding:

- `docs/requirements.md`
- `docs/architecture.md`
- `docs/api-contract.md`
- `docs/test-plan.md`

## Your goal

Implement the local administrative JSON API and real process metrics. Keep benchmark execution/output (M4) and dashboard work (M5) out of this branch. Do not alter the established HTTP scope or add endpoints that are not in the contract.

## Six documented API routes

Run the admin listener on IPv4 loopback `127.0.0.1:9090` in the **same C process** that owns the proxy listener `127.0.0.1:8080`.

Implement exactly these application routes, with the exact response shapes/status/error semantics in `docs/api-contract.md`:

1. `GET /api/v1/health` — administrative API responsiveness only; do not infer proxy readiness or backend health.
2. `GET /api/v1/overview` — proxy listener readiness, policy, backend counts, lifetime counters, live active counts, 60-second rates and latency summary.
3. `GET /api/v1/backends` — exactly three backends in stable ID order, their health state, in-flight reservations, attempt/success/failure counts, latest probe timestamp/latency and measured 60-second attempts-per-second/traffic percentage.
4. `PUT /api/v1/policy` — accept only JSON `{"policy":"round_robin"}` or `{"policy":"least_connections"}`; invalid requests must leave the policy unchanged; response reports the synchronized active value.
5. `GET /api/v1/metrics?window_seconds=N` — real one-second UTC-aligned time-series points, with only completed observed buckets fully inside the requested 10–300 second window.
6. `GET /api/v1/benchmarks/latest` — read only `<absolute-results-dir>/latest.json`; return `{"available":false,"run":null}` when missing. It must **not** start a benchmark or accept a path/query parameter. Actual benchmark file writing/client belongs to M4.

OPTIONS is preflight only for the specified CORS behavior; it does not add another application/data endpoint. Allow only the exact development origins `http://localhost:5173` and `http://127.0.0.1:5173`; never use wildcard CORS.

## Metrics requirements

Implement synchronized counters and bounded rings using the definitions already fixed in the specs. Especially preserve these distinctions:

- `active_connections` counts admitted proxy TCP connections, including request-header parsing; `active_requests` counts only valid parsed requests still executing. Exclude admin and health-probe sockets.
- Separate valid `requests_total`, fully forwarded `responses_forwarded_total`, final valid-request `proxy_failures_total`, client rejections, client disconnects, client response bytes and per-backend dispatched attempts/failures.
- A backend's attempt counter includes failed attempts and retries. Health probes must not change traffic counters, user traffic rates, byte totals, or latency percentiles.
- Count a final failed client request once, regardless of retry count. Client disconnects and invalid requests have the exact separate accounting in the requirements.
- Store at most 3,600 one-second buckets and 100,000 latency samples. Percentiles use nearest-rank over retained observations; no samples means JSON `null`, not zero. Idle time contributes zero to rate denominators as specified.
- Keep `/api/v1/backends`'s attempts-per-second and traffic percentages based on measured attempt deltas in the same last 60-second window, not lifetime counters or fixtures.
- Counters must stay at or below `9,007,199,254,740,991`, the largest exactly represented JavaScript integer.

## HTTP, resource and security constraints

- Preserve the specified 16 KiB admin header limit, 4 KiB body limit, one request per admin connection, 2-second admin read/write deadlines, strict route/method/query/content-type/JSON validation and shared JSON error envelope.
- Bind only to IPv4 loopback. The API must never accept arbitrary backend hosts, caller-provided file paths, shell commands, or any open-proxy behavior.
- `--results-dir` must be an absolute, existing, accessible directory; canonicalize it once at startup. API reads only its fixed `latest.json` child and reports the display basename, never an absolute filesystem path.
- No network I/O while holding shared-state locks. Use the pool's synchronization or add a small policy getter/setter API so policy changes and backend snapshots are race-safe.
- Ensure the main process owns both listener lifetimes cleanly. Preserve M2's routing/health behavior while adding admin concurrency limits and shutdown cleanup.
- Do not display fabricated counters, sample series, benchmark data or percentages.

## Tests and completion gate

Add tests before considering M3 done: every route's success and unsupported method/path cases; bad/duplicate/unknown query keys; policy success plus malformed JSON, extra fields, wrong type and invalid enum; 4 KiB body boundary and media type; allowed/disallowed CORS preflight; stable backend snapshot values; zero-data rates and null percentiles; metric bucket/window math; probe exclusion; backend traffic rate and zero-denominator cases; missing/valid/corrupt latest benchmark file; startup failure for invalid `--results-dir`; and consistency between API JSON and TypeScript-facing contract examples.

Run the macOS warnings-as-errors workflow and CTest. Report exact commands, observed results, what remains untested, and the commit/PR. Do not report tests as passed unless the CI logs confirm them. Keep README accurate; M4 benchmark generation and M5 dashboard remain unchecked.
