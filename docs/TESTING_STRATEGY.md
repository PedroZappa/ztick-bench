# Testing Strategy

| | |
| --- | --- |
| **Status** | Technical specification — v1.0 baseline complete |
| **Source of truth** | [DECISIONS.md](DECISIONS.md) BR §5.2, §14, §16–§17, §21–§22 |
| **Authoritative for** | Test layers, custom SDK test applications, acceptance-criteria traceability, CI expectations |
| **Not authoritative for** | SDK contract ([SDK](SDK_SPECIFICATION.md)), metric methods ([METRICS](METRICS_REFERENCE.md)), result encoding ([RESULTS](RESULTS_FORMAT.md)) |

---

## 1. Testing requirements

**REQ-TEST-01 (Required).** At least two independent custom test applications must
validate the SDK integration model for v1.0. *Source: BR §5.2, §17.2.*

**REQ-TEST-02 (Required).** Automated tests cover correctness, measurement
overhead, repeatability, failure handling and plugin isolation. *Source: BR §17.7.*

**REQ-TEST-03 (Required).** The SDK and core engine must be validated through
working custom applications before investing heavily in the TUI. *Source: BR §16.*

**REQ-TEST-04 (Confirmed).** Catch2 v3 is the test framework, pinned as a dev-only
dependency and integrated with `find_package` plus a FetchContent fallback. It is
never linked into installed core, SDK, CLI or TUI targets. *Source: BR §22.*

## 2. Test layers

| Layer | Scope | Examples |
| --- | --- | --- |
| Unit | Single components with deterministic inputs | Generators, statistics formulas, config parsing, status encoding |
| Integration | Multiple components with real adapters | SDK ABI calls, capability rejection, validation hooks |
| System | Full runs through CLI/core | Profiles, result writes, comparisons, failure paths |
| Acceptance | Checks mapped to BR §17 | Criteria table in §10 |

Catch2 v3 is used across all layers above (REQ-TEST-04). Its sections and
parameterised tests cover the statistics, generator, validation, overhead, timing
and failure cases in §4–§9. The version is pinned for offline/reproducible builds;
the FetchContent fallback is a build-time convenience only and does not make
Catch2 a runtime dependency. The pinned release is Catch2 v3.9.1 (commit
`644821ce28cb25d7992a4d0375b1d83214392592`, BSL-1.0); it is dev-only and is
never linked into installed SDK, core, CLI or TUI targets.

**Unit-test scope (Derived design).** Every function introduced or modified in
ztick-bench has a deterministic unit test asserting its observable behavior,
including error and edge paths, before the change is considered complete. Generated
or vendored code and declarations without a definition are exempt; any other
exception must be recorded in the change. Tests are registered with CTest
(`make test` runs them). Formulas taken from
[METRICS §3](METRICS_REFERENCE.md#3-definitions-and-formulas) are checked against
an independently computed expected value, not against the implementation under
test.

## 3. Custom SDK test applications

The requirement is two **independent** applications (BR §5.2). Derived design of
a complementary pair:

| App | Packaging | Coverage focus |
| --- | --- | --- |
| A — processor/chain harness | Static integration compiled into the host | Processor and chain workloads, typed parameters, Basic/Standard validation, reference data, per-block timing |
| B — engine harness | Dynamically loaded shared library | SDK version/capability negotiation, engine workload, internal multithreading, instrumented measurements, Strict validation, out-of-process execution |

**Confirmed:** these two applications remain generic placeholders; no specific
application or DSP library is mandated (BR §21).

Both apps must exercise the full adapter contract in
[SDK_SPECIFICATION](SDK_SPECIFICATION.md) and must be buildable without modifying
ztick-bench itself (BR §17.8). They are distinct applications, not two configurations
of one binary, so a shared defect cannot validate both.

## 4. Correctness tests

- Generate deterministic inputs (sine, multi-tone, impulse) with fixed parameters
  and verify generator reproducibility (BR §4.3).
- Exercise all validation levels: NaN/Inf injection, out-of-range/clipping cases,
  reference comparisons with absolute/relative tolerances, expected output change
  detection, adapter custom checks and reference-implementation comparison
  (BR §10).
- Verify validation runs outside the timed interval and cannot change timing
  results (BR §10).
- Verify result encoding of measured/estimated/unavailable/N-A statuses and of
  validation outcomes ([RESULTS](RESULTS_FORMAT.md#3-result-document-structure)).

The Phase 2 verification pass (ROADMAP P2.8) is an integration test in
`tests/integration/phase2_engine_test.cpp`. It chains deterministic generation,
all three timing levels, validation and the METRICS §3 aggregates into one run,
checking each expected value against an independent calculation so the Phase 2
exit criterion is demonstrated across component boundaries.

## 5. Measurement overhead and timing tests

- Verify aggregate mode is the default and per-block collection is opt-in
  (BR §7.3).
- Measure timing overhead at small block sizes (16/32 samples) and verify overhead
  estimates are reported (BR §7.3).
- Verify raw samples and overhead estimates are stored independently and that no
  subtraction occurs before reporting (BR §7.3, REQ-MET-03).
- Verify all three timing levels: per-block, whole-run and explicit end-to-end,
  including that generation/setup are outside the primary interval by default
  (BR §7.2, §7.4).
- Verify metric computations against independent reference calculations for
  mean, median, percentiles, standard deviation, throughput, RTF and scaling
  efficiency ([METRICS](METRICS_REFERENCE.md#3-definitions-and-formulas)).

## 6. Repeatability and stability tests

- Run-to-run comparisons on the same build to establish variability (BR §11.2).
- Verify configurable thresholds and significance checks: differences below
  threshold or within normal variability must not be reported as regressions
  (BR §11.3, [METRICS](METRICS_REFERENCE.md#6-comparison-and-regression-statistics)).
- Verify build-to-build and machine-to-machine comparison paths, including
  comparability warnings when metadata differs (BR §11.2–§11.3).
- Verify warning-not-reject behavior under simulated thermal/frequency/load
  instability: warning is recorded, measurements retained, run not claimed fully
  controlled (BR §9).
- Verify convenience and controlled reliability modes select their documented
  warm-up/repetition/duration behavior (BR §9).

## 7. Failure-handling and integrity tests

- Inject workload failures (adapter crash, invalid output, processing failure)
  and infrastructure failures (load failure, monitoring unavailable) and verify
  classification and context recording (BR §14).
- Verify completed measurements survive a later failure and independent tests
  continue where safe (BR §14).
- Verify a crashing plugin in out-of-process mode does not terminate the host CLI
  or TUI (BR §14, [ARCHITECTURE](ARCHITECTURE.md#6-failure-containment)).
- Verify incomplete runs are marked incomplete and never rendered as successful
  complete benchmarks, in JSON, CSV and UI projections.
- Verify result writes are atomic: simulate failure during serialization and
  confirm existing result files remain valid
  ([RESULTS](RESULTS_FORMAT.md#9-failure-and-warning-encoding)).

## 8. Plugin discovery, trust and isolation tests

- Verify discovery of explicit paths, user directory and project-local directory
  without executing discovered plugins (BR §6.1).
- Verify identity/version/SDK compatibility/capabilities are shown before use and
  that first execution of an untrusted plugin requires explicit confirmation
  (BR §6.2).
- Verify both in-process and out-of-process modes, that the used mode is recorded,
  and that isolation is not described as a complete security sandbox (BR §6.3).
- Verify incompatible SDK versions and unsupported configurations are refused with
  an explanation rather than executed
  ([SDK](SDK_SPECIFICATION.md#2-versioning-and-compatibility)).

## 9. Portability, comparison and CI tests

- Round-trip portable config documents and result documents, including metadata
  and session export/import (BR §12.3, §13).
- Load a config on a simulated different machine and verify unsupported
  capabilities/environment differences are reported and the test is not silently
  changed (BR §13).
- Verify CLI config files, command-line overrides, CI-friendly exit codes and
  machine-readable output (BR §12.2).
- Verify build metadata capture (git commit, compiler, flags, configuration)
  where discoverable (BR §11.1).

## 10. Acceptance criteria traceability

| BR §17 criterion | Verifying tests |
| --- | --- |
| 1. Linux fully supported | System tests on Linux; platform fields checked for `unsupported`/`unavailable` reporting |
| 2. Two independent SDK test apps | §3 apps A and B built and run |
| 3. Static and dynamic integrations | §3 apps A/B; §8 discovery/load tests |
| 4. Initial metrics and workload levels | §4, §5, §6 tests across processor/chain/engine |
| 5. CLI, JSON, comparisons, regressions | §9 tests; §6 comparison tests |
| 6. TUI principal workflows | System tests through dashboard, config, execution monitoring and results screens ([TUI](TUI_SPECIFICATION.md)) |
| 7. Automated correctness/overhead/repeatability/failure/isolation tests | §4–§8 |
| 8. Third-party integration docs | [SDK_SPECIFICATION](SDK_SPECIFICATION.md) conformance checklist validated by an external-style build of app B |

## 11. Derived versus required

Test cases in §2–§9 are derived design implementing REQ-TEST-01–03 and the ledger
requirements they cite. They do not add new v1.0 requirements.
