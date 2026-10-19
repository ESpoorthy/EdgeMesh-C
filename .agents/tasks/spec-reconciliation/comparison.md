# EdgeMesh-C Spec Reconciliation — Cross-Document Comparison

**Produced by:** spec-reconciliation workflow step  
**Source files examined (in full):**
- `AGENTS.md`
- `docs/requirements.md`
- `docs/architecture.md`
- `docs/api-contract.md`
- `docs/test-plan.md`

---

## Summary

Eight categories of ambiguity, contradiction or gap were found. No source file was modified. Every finding below cites the exact document(s) involved.

---

## 1. HTTP Status Code Mapping

### What is documented

| Code | Mentioned in | Context |
|------|-------------|---------|
| 200 | api-contract.md §3, §6, §8 | Health endpoint; successful policy update; benchmark unavailable |
| 400 | api-contract.md §1, §6, §7; requirements.md FR-01; test-plan.md §3 | Malformed/invalid admin requests |
| 404 | api-contract.md §1 | Unknown route |
| 405 | api-contract.md §1 | Unsupported HTTP method on admin routes |
| 413 | api-contract.md §1 | Oversized admin request body |
| 500 | api-contract.md §1, §8 | Internal error; unreadable benchmark result |
| 503 | requirements.md FR-02; architecture.md §3 step 4; test-plan.md INT-07 | No healthy backend |

### Gaps and ambiguities

**G1-A — 503 absent from api-contract.md.**  
`api-contract.md §1` lists the general status-code table (404, 405, 400, 413, 500) for admin routes but never mentions 503. The proxy is a different listener (`:8080`), but the contract document is the single cross-team schema reference. It contains no section describing proxy-side error responses. The proxy listener's status codes (400 for bad requests, 503 for no healthy backend) are described only in `requirements.md` and `architecture.md`, not in `api-contract.md`. If the dashboard ever fetches directly from `:8080`, the contract is incomplete.

**G1-B — 400 vs "clear errors" for malformed proxy requests.**  
`requirements.md FR-01` says "return clear errors for malformed, oversized or unsupported requests" and `test-plan.md §3` says "rejected safely with the documented status" but neither document names the exact status code (400? 501? 415?) for each rejection category on the proxy side. `api-contract.md §1` documents 400 only for the admin API. There is no table of proxy-listener error codes anywhere.

**G1-C — `proxy_failures_total` counting rule for rejected client input.**  
`api-contract.md §4` and `requirements.md FR-04` both say "Track rejected client input separately where practical." This implies rejected-client-input requests may or may not be included in `proxy_failures_total`. No document resolves when "not practical" applies or what field carries those counts instead. The test-plan (§3 Counters row) does not enumerate this edge case.

---

## 2. Timeout Values

### What is documented

| Timeout | Mentioned in | Value given? |
|---------|-------------|-------------|
| Client read timeout (proxy `:8080`) | requirements.md FR-01; architecture.md §3 step 2 | **No** — "bounded" only |
| Backend connect timeout | requirements.md FR-01; architecture.md §3 step 6 | **No** — "bounded" only |
| Backend read/response timeout | requirements.md FR-01; architecture.md §3 step 7 | **No** — "bounded" only |
| Health-probe timeout | requirements.md FR-03; architecture.md §4 | **No** — "bounded" only |
| Admin request read timeout | api-contract.md §1 (implies from header/body limits) | **No** — not stated |

### Gaps and ambiguities

**G2-A — No numeric timeout values anywhere.**  
Every document uses the word "bounded" without specifying a default or range. There is no constant, table or configuration key that gives an implementer a starting number. This means two independently written components (proxy vs health checker) could choose wildly different defaults without any spec violation.

**G2-B — Timeout configuration surface is undefined.**  
No document says whether timeouts are compile-time constants, a configuration file or command-line flags. `architecture.md §4` mentions "configurable thresholds" for health checks but only in the context of failure/success counts, not timeouts.

**G2-C — Admin listener timeout absent.**  
`api-contract.md §1` caps header size at 16 KiB and body at 4 KiB but says nothing about how long the server waits for a slow or stalled client. A missing read timeout on the admin listener can cause a thread to block indefinitely.

---

## 3. Health Thresholds

### What is documented

| Threshold | Mentioned in | Value given? |
|-----------|-------------|-------------|
| Consecutive failures before unhealthy | requirements.md FR-03; architecture.md §4; test-plan.md §3 Health state row | **No** — "documented threshold" cited but never stated |
| Consecutive successes before recovery | requirements.md FR-03; architecture.md §4; test-plan.md §3 Health state row | **No** — "recovery threshold" cited but never stated |
| Probe interval | requirements.md FR-03; architecture.md §4 | **No** — "periodic" only |

### Gaps and ambiguities

**G3-A — Self-referential "documented threshold."**  
`test-plan.md §3` says "State changes at the documented threshold" and `requirements.md FR-03` says "Use documented consecutive-failure and consecutive-success thresholds." Neither document is the one that actually documents the numbers. They each point to a threshold that exists nowhere in the five files.

**G3-B — "Unknown" initial state transition.**  
`api-contract.md §5` lists `state` as exactly `healthy`, `unhealthy`, or `unknown`. Neither `requirements.md` nor `architecture.md` specifies how many consecutive successes are needed to leave the `unknown` initial state. It is unclear whether `unknown → healthy` uses the same success threshold as `unhealthy → healthy` or a different (possibly zero) threshold.

**G3-C — Probe interval not specified.**  
`architecture.md §4` says probes run "in a worker or equivalent scheduled mechanism" with "bounded timeouts." No document gives a default probe interval (e.g., every N seconds). The test-plan (INT-05) says a backend "becomes unhealthy within configured thresholds" but never states what those thresholds are, making the test non-deterministic without additional specification.

---

## 4. Rate Calculation Semantics

### What is documented

- `api-contract.md §4`: `requests_per_second` and `response_bytes_per_second` "describe the most recent sample window."
- `api-contract.md §7`: `GET /api/v1/metrics?window_seconds=60` (range 10–300, default 60) returns time-series points.
- `api-contract.md §9`: `response_bytes_per_second` is "response bytes forwarded by the proxy divided by the observation interval. Document consistently whether headers are counted."
- `requirements.md FR-04`: "Define p50/p95/p99 over the documented rolling sample window."

### Gaps and ambiguities

**G4-A — "Most recent sample window" vs. the query `window_seconds` parameter.**  
`api-contract.md §4` (overview endpoint) exposes `requests_per_second` described as "the most recent sample window" and `sample_window_seconds: 60` (fixed in the example). `api-contract.md §7` lets callers choose `window_seconds` from 10 to 300. It is unspecified whether the overview's rate and `sample_window_seconds` are always the fixed default (60 s), or whether there is a process-wide rolling window whose size can differ from a per-request query parameter. An implementer cannot tell if changing the query window changes the overview's rate fields.

**G4-B — Low-traffic and startup behaviour for rates.**  
No document specifies what the rate fields return when fewer than one full window has elapsed (startup) or when no requests arrived during the window (idle). The null-vs-0.0 rule is stated only for latency percentiles ("return null"). `requests_per_second: 0.0` and `requests_per_second: null` are both plausible at startup — but the contract only mandates null for percentiles, leaving rates ambiguous.

**G4-C — Whether headers are counted in `response_bytes_per_second`.**  
`api-contract.md §9` says "document consistently whether headers are counted" but never actually makes that choice. This is an explicit open question in the contract itself, not a gap introduced by cross-document skew. The dashboard rendering and benchmark comparison both depend on this being settled.

**G4-D — Rolling window vs. fixed-interval buckets.**  
No document specifies whether the window is a true sliding window (drops individual samples as they age) or a sequence of fixed-length buckets. The distinction affects both memory usage (sample retention) and the exact latency percentile values at any given moment.

---

## 5. Health-Probe Traffic and Proxy Metrics

### What is documented

- `requirements.md FR-04`: Defines what counts as a "successfully forwarded response" and a "proxy failure" for user-facing traffic.
- `api-contract.md §4` and §5: Define `requests_total`, `responses_forwarded_total`, `proxy_failures_total` at both aggregate and per-backend level.
- `requirements.md FR-03`: Health probes use "bounded timeouts" and run in a separate worker.

### Gap

**G5-A — No document states whether health-probe connections increment any metric.**  
Health probes connect to backends and receive responses. None of the five files says whether those probe connections are included in or excluded from:
- Per-backend `requests_total` / `responses_forwarded_total` / `proxy_failures_total` (api-contract.md §5)
- Aggregate `requests_total` and related counters (api-contract.md §4)
- Latency samples used to compute percentiles

If probe traffic is counted, idle periods will show non-zero request rates and latency distributions skewed by probe latency. If it is excluded, the implementation must carefully tag or route probe results separately. The test-plan does not include a test case that distinguishes probe vs. proxy traffic in metrics.

---

## 6. Incomplete/Partial Upstream Response Counter Semantics

### What is documented

- `requirements.md FR-04`: "A proxy failure includes no healthy backend, connect/read timeout, or **invalid/incomplete upstream response**."
- `api-contract.md §4`: Same list, ending with "invalid/incomplete upstream response."
- `architecture.md §3` step 7: "Validate the upstream status/header section within a bounded size, then forward the response while handling partial writes and timeouts."

### Gaps and ambiguities

**G6-A — "Incomplete" upstream response is not defined.**  
No document specifies what constitutes an "incomplete" upstream response. Candidates include:
- Connection reset after headers but before body ends
- `Content-Length` mismatch (body shorter than declared)
- Body truncated mid-transfer
- Upstream closes connection without sending any status line

The distinction matters because: if headers were already forwarded to the client before the upstream truncates the body, the proxy has already sent a 200 to the client. The document does not say whether that scenario increments `proxy_failures_total` or `responses_forwarded_total` (or both, or neither, with a new counter).

**G6-B — Double-counting risk.**  
Because a "forwarded response" is defined as "valid upstream HTTP response received and forwarded, including upstream 4xx/5xx," a response where headers were forwarded but the body was not fully delivered could fit both the "forwarded" and "failure" definitions. No document resolves whether these counters are mutually exclusive.

---

## 7. Connection Limits

### What is documented

- `AGENTS.md` engineering requirements: "Bound buffers, request sizes, concurrent connections, metrics history and admin body sizes."
- `requirements.md §4`: "Validate lengths and values … avoid unbounded buffers and unchecked allocation."
- `architecture.md §5`: "Define an explicit maximum active connection count and predictable overload behaviour."
- `api-contract.md §1`: Maximum admin header 16 KiB, maximum admin body 4 KiB.
- `architecture.md §3` step 1: "Accept a TCP client on 127.0.0.1:8080, respecting the configured connection limit."

### Gaps and ambiguities

**G7-A — No numeric connection limit specified for the proxy listener.**  
`architecture.md §5` says to "define an explicit maximum" but never states what it is. The proxy's maximum active connection count is unspecified across all five documents.

**G7-B — No numeric connection limit specified for the admin API listener.**  
The admin API's maximum concurrent connections is not stated. Because the admin API shares the same process, an unbounded admin listener could starve proxy worker threads. The document says "proxy work, health probes and admin requests must not block one another indefinitely" (architecture.md §5) but sets no cap.

**G7-C — Overload behaviour unspecified.**  
`architecture.md §5` says "predictable overload behaviour" but does not say whether the server silently drops excess TCP connections, sends a 503, or queues them. This affects INT-07 (all backends down), where a burst of retrying clients could exhaust connection slots before the 503 path is exercised.

**G7-D — `active_requests` vs. `active_connections`.**  
`api-contract.md §4` exposes `active_requests` (in-flight proxy requests). `api-contract.md §5` exposes per-backend `active_connections`. `architecture.md §3` step 1 uses "connection limit." These three terms are used without a cross-reference clarifying whether `active_requests` == total active proxy connections or whether connections and in-flight requests can diverge (e.g. a connected client that has not yet sent a complete request).

---

## 8. Benchmark Results File Path

### What is documented

- `api-contract.md §8`: The API reads "only a fixed documented result path." The example response contains `"result_file": "results/latest.json"`. The section says "The API reads only a fixed documented result path; it must never accept an arbitrary path from the caller."
- `architecture.md §6`: "The benchmark tool saves actual results locally. The API reads only a fixed, documented result location."
- `architecture.md §7` (target layout): Shows a top-level `results/` directory.
- `requirements.md FR-06`: "Save actual completed-run results in JSON and/or CSV." No path mentioned.
- `AGENTS.md`: "Keep secrets and machine-specific paths out of source control."

### Gaps and ambiguities

**G8-A — The fixed path is never fully defined.**  
`api-contract.md §8` shows `"result_file": "results/latest.json"` inside the example response payload, but does not explicitly say this is the canonical read path on disk. It is the value returned in JSON, which could be a logical name rather than a filesystem path. No document states the absolute or process-relative path the C process actually opens.

**G8-B — Relative path anchor (process cwd) is unspecified.**  
`architecture.md §7` places `results/` at the repository root, implying the process must be launched from the repository root for the relative path to resolve. No document mandates the working directory at launch, creates a symlink, or allows the path to be configured. If the binary is launched from a different directory (e.g., `build/`), the path silently fails to resolve and the endpoint returns 500 (per api-contract.md §8), with no guidance for the operator.

**G8-C — JSON vs. CSV ambiguity.**  
`requirements.md FR-06` says "Save actual completed-run results in JSON and/or CSV." `api-contract.md §8` shows the GET endpoint returning a JSON object that includes a `result_file` field. The schema does not indicate whether the C API reads a JSON file, a CSV file, or both. If the benchmark writes only CSV, the API cannot deserialize it without an unspecified conversion step.

**G8-D — Multiple runs — only `latest` is addressed.**  
`api-contract.md §8` is named `GET /api/v1/benchmarks/latest` and returns a single run object. `requirements.md FR-06` says "Save actual completed-run results" (plural implied). No document specifies whether each run overwrites `results/latest.json`, appends to a log, or writes a timestamped file. The convention is implied by "latest" but not stated.

---

## Cross-Cutting Observations

1. **"Documented" as a self-reference.** Several requirements cite "the documented threshold" or "configured thresholds" without the documentation being present in any of the five files. This pattern appears in: FR-03 (health), architecture.md §4, test-plan.md §3 Health state row, and test-plan.md INT-05/INT-06. A single shared constants section or configuration schema is missing.

2. **Null vs. 0.0 policy is partial.** The null rule is explicit only for latency percentiles. It is unspecified for `requests_per_second`, `response_bytes_per_second`, `last_response_ms`, and `latency_p50_ms` in per-backend records when no traffic has arrived.

3. **"Track rejected client input separately where practical"** appears identically in both `requirements.md FR-04` and `api-contract.md §4` without defining what "separately" means in terms of API fields or what "not practical" permits.

4. **Test INT-14** ("Repeat a seeded test") implies a deterministic seed mechanism exists somewhere, but no document describes how the seed is set or what it controls. This could be a health-check ordering seed, a benchmark RNG seed, or something else.

5. **Platform target inconsistency.** `AGENTS.md` and `requirements.md` target macOS and POSIX. `test-plan.md §2` states "Environment: macOS, C17, CMake, CTest." The current workspace is Windows (`platform: win32`). No document addresses cross-platform build support or clarifies that the MVP is macOS-only for contributors working on other platforms.

---

*End of comparison. No spec files were modified. No implementation code was produced.*
