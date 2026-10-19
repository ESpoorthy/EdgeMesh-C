# Spec Reconciliation — End-of-Task Report

**Task:** Reconcile the five EdgeMesh-C specification documents into a single, self-consistent v1.1 baseline before implementation begins.  
**Branch:** `docs/spec-reconciliation`  
**Commit:** `b694e8e`  
**Date:** 2026-10-19

---

## 1. Decisions finalised A–H

**A — HTTP status mapping**  
A single authoritative HTTP status table was added to `docs/requirements.md §3` and cross-referenced from both `docs/architecture.md` and `docs/api-contract.md`, so every error condition (malformed request, timeout, connection-limit exceeded, oversized body, unknown route, etc.) maps to exactly one documented status code that all three documents agree on.

**B — Timeout constants**  
All seven per-operation timeout values (client read, backend connect, backend response-header read, overall per-request deadline, health-probe connect, health-probe read, admin read) were consolidated into a single authoritative table in `docs/architecture.md §5a` and cross-references were added wherever the values were previously stated inline, eliminating the risk of an implementer using a different value read from a different file.

**C — Health thresholds**  
The three health-check parameters — 3 consecutive failures to mark a backend unhealthy, 2 consecutive successes to recover, and a 5 s probe interval — were written explicitly into `docs/architecture.md §4` and mirrored verbatim into `docs/requirements.md FR-03`, ensuring test-plan thresholds and implementation thresholds are derived from the same authoritative source.

**D — Rate calculation semantics**  
`requests_per_second` and `response_bytes_per_second` were defined precisely as a rolling window of per-second buckets over `sample_window_seconds`, with a clear rule that these fields return `0.0` (never `null`) when fewer than two complete buckets exist, distinguishing them from percentiles which return `null` when no samples exist; this definition was written into both `docs/requirements.md FR-04` and `docs/api-contract.md §9`.

**E — Incomplete upstream response counters**  
Two distinct partial-failure paths were given unambiguous counter rules: if response headers are never received (connect timeout, read timeout, invalid status line) then `proxy_failures_total` is incremented and `responses_forwarded_total` is not; if forwarding begins but the upstream body drops mid-transfer then `responses_forwarded_total` is incremented and `bytes_forwarded` counts only bytes actually delivered to the client — these rules appear in `docs/requirements.md FR-04` and are repeated in `docs/api-contract.md §9`.

**F — Benchmark result path**  
The benchmark tool writes results to `results/latest.json` resolved relative to the executable's own path at startup (not the process working directory), and the admin API reads only that same fixed path — it never accepts a caller-supplied path; this rule was added to `docs/architecture.md §6` and echoed in `docs/api-contract.md §8`.

**G — Connection limits**  
Explicit maximums were stated in `docs/architecture.md §5` (128 concurrent proxy connections, 16 concurrent admin connections) with the overload behaviour for each (accept then immediately respond 503 and close), and mirrored in `docs/requirements.md FR-01` and `FR-05`, so the C implementation has a single number to enforce with no ambiguity about what to do when the limit is reached.

**H — AGENTS.md reference bullet**  
A bullet was added to the Engineering requirements section of `AGENTS.md` instructing implementers to use only the timeout values, connection limits, health thresholds and HTTP status codes from their respective authoritative tables (architecture.md §5a, requirements.md §3, architecture.md §4) and to update those tables before introducing different values, closing the loop between the agent instructions and the spec documents.

---

## 2. Files changed

**`docs/requirements.md`** — Added the complete HTTP status mapping table in §3, explicitly defining the status code for every documented proxy and admin error condition. Expanded FR-03 to state the three health thresholds verbatim (matching architecture.md §4). Expanded FR-04 with the precise counter-split rules for partial upstream failures and the `0.0`-not-null rule for rate fields. Added the explicit 128/16 connection-limit numbers to FR-01 and FR-05.

**`docs/architecture.md`** — Added the Timeout and retry constants table as §5a, listing all seven timeout values in one place. Added the connection-limit maximums and overload behaviour explicitly in §5. Expanded §4 (Backend state and selection) to state all three health-threshold values inline. Added the benchmark result path resolution rule to §6 (Admin API and dashboard).

**`docs/api-contract.md`** — Added the rate-field semantics and the `0.0`-not-null rule to §9 (Metric definitions), and added the two partial-upstream-failure counter rules. Added a cross-reference in the General rules section pointing to the HTTP status mapping table in requirements.md rather than duplicating it. Clarified §8 (GET /api/v1/benchmarks/latest) to state that the result path is resolved from the executable path at startup, never from the process working directory or a caller-supplied path.

**`docs/test-plan.md`** — Updated the Health state row in the Unit tests table (§3) to reference the exact documented thresholds (3 consecutive failures, 2 consecutive successes) so test authors have a direct pointer to the numbers rather than a vague description.

**`AGENTS.md`** — Added one Engineering requirements bullet directing agents to use only the values from the three authoritative tables and to update those tables before changing any value, preventing silent drift between agent-generated code and the spec.

---

## 3. Files preserved unchanged

`git -C c:\Users\HARI\Downloads\flutter\edgemesh diff HEAD~1 -- LICENSE README.md .gitignore` produced no output (exit code 0), confirming that none of those three files were touched by the reconciliation commit.

---

## 4. Git commands and output

The reconciliation commit was created and pushed as part of the prior workflow step. The exact commit record is:

```
commit b694e8e4810dff96e917ce3fb8087a4a04f89047
Author: Kommera Hari Hansika <25wh5a6601@bvrithyderabad.edu.in>
Date:   Mon Oct 19 20:34:28 2026 +0530

    docs: reconcile spec v1.1 - explicit timeouts, status codes, thresholds,
    rate semantics, counters, benchmark path, connection limits

 AGENTS.md            |  1 +
 docs/api-contract.md |  8 ++++++--
 docs/architecture.md | 23 +++++++++++++++++++++--
 docs/requirements.md | 33 ++++++++++++++++++++++++++++-----
 docs/test-plan.md    |  2 +-
 5 files changed, 57 insertions(+), 10 deletions(-)
```

The branch `docs/spec-reconciliation` was pushed to `origin` (https://github.com/Kommera-Harihansika/EdgeMesh-C.git).

---

## 5. Branch and PR

**Branch:** `docs/spec-reconciliation`  
**Remote:** `origin/docs/spec-reconciliation` — pushed successfully (commit `b694e8e` confirmed on remote).

**PR:** No PR was created automatically by this workflow step. Open a pull request manually at:  
https://github.com/Kommera-Harihansika/EdgeMesh-C/compare/docs/spec-reconciliation

The PR title should be: `docs: reconcile spec v1.1 — timeouts, status codes, thresholds, rate semantics, counters, benchmark path, connection limits`

---

## 6. Remaining risks and ambiguities

1. **`response_bytes_per_second` header inclusion** — The spec states that whether response headers are counted in byte throughput should be "documented consistently" but does not make a final choice. The implementation team must pick one interpretation (headers included or excluded) and record it in api-contract.md §9 before writing the metrics module.

2. **Rolling-window bucket granularity** — The rate fields are defined as "a rolling window of per-second buckets", but the bucket width (1 s) and total history size (number of retained buckets) are not stated. The implementation must pick concrete values and add them to architecture.md §5 to keep the metrics module and the dashboard rendering consistent.

3. **Latency sample window vs. rate window** — `sample_window_seconds` governs both the rate calculation and the latency percentile window. If these need to diverge (e.g. a longer latency window for a smoother p99), a second parameter would be required; that decision should be made before the metrics module is written.

4. **Benchmark result file format** — `results/latest.json` is the defined path, but the exact JSON schema for what the benchmark tool writes is only described via the API response schema in api-contract.md §8. The benchmark module author should treat that schema as the file schema too; any extension fields should be documented in api-contract.md before use.

5. **Deterministic tie-breaking in least-connections** — "Deterministic rotation among ties" is described but the rotation state (e.g. a global round-robin cursor shared with the round-robin policy, or a separate cursor) is unspecified. The implementation must choose and document this to ensure INT-04 is reproducible.

6. **Health state `unknown` transition** — The spec defines `unknown` as the initial state but does not specify whether a backend re-enters `unknown` after a restart or stays in its last known state until probes resolve it. Implementers should decide and add a note to architecture.md §4.

7. **Admin API OPTIONS preflight scope** — The spec says "Support OPTIONS preflight for API routes" but does not list which specific routes need preflight handling (all six, or only the PUT policy route). Because only PUT changes state, a future implementer should confirm whether GET routes also need CORS preflight in practice.

8. **No CI configuration exists yet** — The planned build/test commands (`cmake -S . -B build`, `ctest --test-dir build`) are documented but there is no CI workflow file. The first implementer to add source files should also add a CI configuration to ensure the build is continuously verified.
