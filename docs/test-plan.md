# EdgeMesh-C Test Plan

**Version:** 1.0 — MVP  
**Environment:** macOS, C17, CMake, CTest

## 1. Strategy

Test from smallest units outward:
1. Unit tests for HTTP parsing, policy selection, admin input validation and metrics calculations.
2. Integration tests that launch the C application and three local demo backends.
3. Failure tests that stop/restart backends and send malformed input.
4. Benchmark experiments comparing the policies under the same workload.
5. Dashboard checks for loading, live values, errors and missing benchmark results.

Manual Wireshark inspection may support the demonstration, but does not replace automated tests.

## 2. Intended build/test workflow

After the build system is implemented, use:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure

These are planned commands and have not been run while writing this document. If actual target names or build options change, update the README and this plan. Use a separate sanitizer build with AddressSanitizer and UndefinedBehaviorSanitizer when the compiler supports them.

## 3. Unit tests

| Area | Cases | Expected outcome |
|---|---|---|
| HTTP parser | Valid GET, missing Host, malformed request line, oversized headers, invalid header syntax, unsupported method, request body | Supported input parses; invalid/unsupported input is rejected safely with the documented status |
| Header handling | Hop-by-hop headers, invalid names/values, conflicting length headers | Unsafe or ambiguous input is rejected or handled according to the limited subset; hop-by-hop headers are not blindly forwarded |
| Round robin | Three healthy backends, one unhealthy, all unavailable | Rotates through eligible servers and skips unavailable ones |
| Least connections | Different in-flight counts, ties, concurrent reservations | Picks the lowest healthy count and uses deterministic tie-breaking |
| Counters | Success, connect failure, timeout and client disconnect | Counters update correctly and active reservations are released exactly once |
| Health state | Consecutive failures/successes at thresholds | State changes at the documented threshold; transient errors do not create uncontrolled oscillation |
| Metrics | Empty history, one sample, multiple samples, window expiry | Percentiles are correct or null when empty; retained history remains bounded |
| Admin input | Valid/invalid policy JSON, missing field, wrong type, oversized body, unknown route | Correct status/error shape; invalid update leaves the policy unchanged |
| JSON output | Strings containing quotes or control characters | JSON remains valid and strings are escaped safely |

## 4. Integration and failure tests

| ID | Scenario | Pass condition |
|---|---|---|
| INT-01 | Start three backends and the proxy | Listeners bind to configured ports; healthy backends are recognised after probes |
| INT-02 | Send GET through proxy | Client receives a valid response identifying a configured backend |
| INT-03 | Round-robin workload | Sequential requests rotate in the documented order; with equal healthy backends and no failures, counts differ by no more than one |
| INT-04 | Least-connections workload | Selection uses actual in-flight counts; a controlled slow-backend test exercises the policy |
| INT-05 | Stop one backend | It becomes unhealthy within configured thresholds; remaining healthy servers continue serving requests |
| INT-06 | Restart a backend | It becomes healthy after the recovery threshold and is eligible again |
| INT-07 | Stop all backends | Proxy returns HTTP 503 without crashing or hanging indefinitely |
| INT-08 | Invalid or unsupported request | A bounded valid error response is sent and the process remains alive |
| INT-09 | Backend timeout or malformed response | Request fails predictably, reservations are released and counters update |
| INT-10 | Exercise all admin endpoints | Responses conform to api-contract.md, including documented error paths |
| INT-11 | Invalid policy update | API returns 400 and the active policy remains unchanged |
| INT-12 | Stop the admin API during dashboard use | Dashboard shows disconnected/error state instead of pretending stale sample data is live |
| INT-13 | No benchmark saved | Benchmark screen displays the API's unavailable/empty state |
| INT-14 | Repeat a seeded test | Configuration and seed are recorded so fault scenarios can be reproduced |

## 5. Benchmark methodology

Treat benchmark results as experiments, not marketing claims.

- Use local proxy and deterministic demo backends.
- Record macOS version, compiler, build type, proxy version, policy, request count, concurrency, path and backend configuration.
- Where time allows, perform a short warm-up followed by at least five measured trials per policy/configuration.
- Use the same workload for round_robin and least_connections.
- Include all-backends-healthy and one-backend-unhealthy scenarios.
- Keep logging settings consistent and avoid high-volume debug output in timed runs.
- Record requested/completed/failed requests, duration, requests per second, response bytes per second, p50/p95/p99 latency, backend distribution and health transitions.
- Record CPU/memory only if the method and tool are reliable.
- Do not assume a policy is faster before measuring. Report actual observations and limitations.

## 6. Manual demonstration checklist

1. Start all three demo backends and show their identities.
2. Start the proxy and admin API.
3. Send requests and show backend distribution.
4. Change policy using the dashboard/API.
5. Run the benchmark and display actual results.
6. Stop one backend and show its unhealthy state while other backends remain available.
7. Stop all backends and demonstrate HTTP 503.
8. Restart a backend and demonstrate recovery.
9. Show test output and a verified successful HTTP response.

## 7. Evidence to preserve

Save build commands/output, CTest summaries, integration logs, benchmark JSON/CSV with configurations, screenshots/video of failover, and a results summary with caveats. Never mark a test passed unless it actually ran and its result was inspected.
