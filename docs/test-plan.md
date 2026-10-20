# EdgeMesh-C Test Plan

**Specification:** 1.2 (MVP) · **Environment:** macOS, C17, CMake, CTest

## 1. Test strategy

Test from pure logic outward: bounded HTTP parsing and policy/metrics units; API route/validation tests; multi-process proxy/backend integration; health failure/recovery and saturation; then dashboard contract/state checks. Deterministic local sockets and controllable demo-backend modes are preferred. Manual packet inspection may supplement but never replace automated checks.

Every run report must state the exact command and observed result. This plan is not evidence that any build or test has run.

## 2. Planned build and test commands

Once CMake targets exist:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Use a separate ASan/UBSan build when supported. Keep deterministic integration ports configurable only within `127.0.0.1`; do not weaken production default binds. Test targets should isolate unit tests from tests that launch processes. Update these commands if the implemented target names/options change.

## 3. Unit tests

| Area | Cases | Expected result |
|---|---|---|
| Request parser | Valid GET, query, missing/duplicate Host, malformed line, invalid tokens/control bytes, >8 KiB line, >16 KiB headers, header timeout | Status exactly matches requirements: malformed/ambiguous framing 400, >8 KiB 414, >16 KiB 431, 2-second incomplete headers 408 |
| Request framing | POST, absolute-form, fragment, nonzero Content-Length, Transfer-Encoding, duplicate/conflicting framing, unsupported method/version | GET body framing 413; unsupported method 405 with `Allow: GET`; unsupported version 505; malformed framing 400; no backend selected |
| Header filtering | Connection-nominated hop headers, Proxy-Connection, safe end-to-end headers | Strip hop-by-hop fields; rewrite Host; add Connection: close |
| Response parser | Valid HTTP/1.0/1.1, Content-Length, close-delimited, duplicate lengths, Transfer-Encoding, malformed status, oversized headers/body, incomplete body | Forward supported response; otherwise bounded 502 if no response byte was sent; if any response byte was sent, close without retry or second response |
| Round robin | Three healthy, unhealthy skipped, wraparound, all unhealthy | Correct eligible order and no selection if empty |
| Least connections | Unequal active counts, equal tie rotation, concurrent reserve/release | Minimum selected; tie cursor rotates; counts remain correct |
| Retry/accounting | Connect error then next backend success; all candidates fail; upstream 404/500; client disconnect | At most three distinct candidates, each with a 2-second total deadline; retry only before any response byte is sent; accepted request ends within 9 seconds; upstream 4xx/5xx is forwarded success; disconnect has separate counter |
| Cleanup | Every success/error/timeout path | Active backend and global reservations return to baseline exactly once |
| Health | Unknown initial; threshold failures/successes; alternating result | Transitions only at 3 failures / 2 successes; opposite count resets |
| Metrics | No data, one sample, nearest-rank values, window expiry, idle intervals, partial response bytes, bounded rings and ring overwrite | Empty rates are zero; percentiles are null if no samples; rolling denominator is `min(window, uptime)`; bucket denominator is one second; only closed observed buckets appear; partial bytes count, incomplete responses do not count forwarded; sample count matches retained cohort |
| Probe accounting | Healthy/failing health checks while proxy metrics are sampled | Probe requests, bytes, errors and latency never alter traffic counters, rates or percentiles; request failures alone do not change health state |
| Admin validation | Route/method/query, policy schema, wrong type, unknown fields, trailing JSON, >4 KiB, content type | Expected status and error envelope; rejected mutation preserves policy |
| JSON | Quotes, backslashes and control bytes | Valid escaped JSON, no truncation or injection |

## 4. Integration and failure scenarios

| ID | Scenario | Pass condition |
|---|---|---|
| INT-01 | Launch three demo backends and application | Expected loopback listeners bind; probes mark healthy after two successes, within the documented recovery bound |
| INT-02 | Valid GET through proxy | Valid response identifies one configured backend; connection closes |
| INT-03 | Round-robin sequential batch | Equal healthy pool count spread differs by at most one |
| INT-04 | Least-connections controlled slow backend | Requests prefer lower live in-flight count; reservation released at completion |
| INT-05 | Stop one backend | Three consecutive failures at 1-second intervals and 250 ms total probe deadline; it stops receiving new work within 5 seconds; others serve |
| INT-06 | Restart failed backend | Two consecutive successful probes at 1-second intervals and 250 ms deadline; healthy/eligible within 4 seconds |
| INT-07 | Stop all backends | Valid request receives HTTP 503 within client timeout; process remains alive |
| INT-08 | Backend fails after selection | At most three distinct attempts, each with 2-second total deadline; retries only before any response byte is sent; success or HTTP 502 within 9-second request deadline; counters match attempts |
| INT-09 | Backend hangs or sends invalid response | Per-attempt deadline produces retry/502; incomplete response after output begins closes with no retry/second response; no leaked reservation or worker |
| INT-10 | All admin endpoints and error cases | Status, JSON types, CORS and error envelopes match `api-contract.md` |
| INT-11 | Invalid policy | 400, current policy unchanged |
| INT-12 | Excessive concurrent connections | At 128 proxy or 8 admin sockets, newly accepted sockets are closed without a response; process stays responsive |
| INT-13 | No benchmark / valid / corrupt saved file; launch from different working directories | daemon and benchmark use same absolute `--results-dir`; `latest.json` is found independent of CWD and the daemon fails startup for a missing/inaccessible configured directory; `available:false` if absent; valid data is returned; corruption gives safe 500 without exposing path |
| INT-14 | Dashboard API outage, empty series, null percentiles | Disconnected and empty states appear; UI does not substitute live-looking values |
| INT-15 | Shutdown under load | Listeners close, workers join or stop predictably, sockets/reservations released |

Failure tests should use controllable backend behavior or terminate the backend process, not depend on timing from an external service. Allow scheduler tolerance listed in requirements. Verify each proxy status mapping: malformed input 400, unsupported method 405 plus `Allow: GET`, unsupported version 505, request-line/target >8 KiB 414, headers >16 KiB 431, body framing 413, 2-second header timeout 408, no healthy backend 503, and exhausted upstream failure 502 only if no response has started; after output starts assert close without a second response. At capacity, assert close without response.

## 5. Benchmark methodology

The C client records each actual run's build/version, OS, policy, fixed local target/path, requested requests, concurrency, start/end or duration, completed/failed counts, response bytes, latency percentiles and backend distribution. Completed means a full parseable HTTP response with status 200–499; failed means transport error, malformed/incomplete response, or HTTP 5xx. Validate `completed + failed = requested` and RPS arithmetic before saving. Run daemon and benchmark with the same absolute `--results-dir`; test from unrelated working directories. Verify each benchmark request terminates by the 10-second client deadline.

Compare policies with identical request path, request count, concurrency, backend state and logging configuration. If time permits, warm up and perform at least five measured trials per scenario. Include all-healthy and one-backend-down cases. Record raw observations or sufficient summaries to reproduce derived metrics. Do not claim a winner from one run; include local-machine and small-workload caveats. CPU/memory are optional and reported only with a reliable measurement method.

## 6. Demonstration sequence

1. Start three backends; show identity and health.
2. Start proxy/admin; request through the proxy and show backend distribution.
3. Set each policy through the API/dashboard and show that only valid values are accepted.
4. Run benchmark and show the actual saved record.
5. Stop one backend and show unhealthy transition plus continued service from others.
6. Stop all backends and show HTTP 503.
7. Restart one backend and show recovery/eligibility.
8. Show unit, integration and failure-test outputs with actual commands/results.

## 7. Evidence and completion gate

Preserve build output, CTest summary, failure-test logs and benchmark JSON/CSV with its configuration. Screenshots or video are useful but secondary. Do not mark any criterion passed until the corresponding test has been run and inspected. A clean checkout should be buildable using the documented workflow before the MVP is called demonstrable.
