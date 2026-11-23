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
parameterised tests are the intended coverage mechanism for the statistics,
generator, validation, overhead, timing and failure cases in §4–§9; cases for
phases not yet implemented are added as those phases land. The version is pinned
for offline/reproducible builds;
the FetchContent fallback is a build-time convenience only and does not make
Catch2 a runtime dependency. The pinned release is Catch2 v3.9.1 (commit
`644821ce28cb25d7992a4d0375b1d83214392592`, BSL-1.0); it is dev-only and is
never linked into installed SDK, core, CLI or TUI targets.

**Rust TUI tests (Derived design).** The Phase 5 TUI is Rust/ratatui (BR §21), so
its unit and screen-state tests use `cargo test`, with rustdoc comments, rustfmt
and clippy; C ABI FFI smoke tests cover the language boundary. They join CTest and
`make verify` from Phase 5, with no Rust dependency for core/CLI-only builds.

**Unit-test scope (Derived design).** Every function introduced or modified in
ztick-bench has a deterministic unit test asserting its observable behavior,
including error and edge paths, before the change is considered complete. Generated
or vendored code and declarations without a definition are exempt; any other
exception must be recorded in the change. Tests are registered with CTest
(`make test` runs them). Formulas taken from
[METRICS §3](METRICS_REFERENCE.md#3-definitions-and-formulas) are checked against
an independently computed expected value, not against the implementation under
test.

**SDK runtime link coverage (Derived design).** Link-level checks call each SDK
runtime symbol from C11, C++20 and an external-style consumer, and unit tests cover
the registry and status-message paths
([SDK_SPECIFICATION §2](SDK_SPECIFICATION.md#2-versioning-and-compatibility)).
Example tests read descriptors without calling lifecycle callbacks, and a
symbol-inspection check confirms the dynamic example embeds no SDK runtime copy. A
static registration check proves the adapter registers once and is discoverable
through the private host registry observers. The App A host harness then drives the
static adapter's full lifecycle; descriptor inspection remains separate from that
execution. Dynamic-loader tests inject a fake library
to cover every argument, load, symbol, header and compatibility failure path, assert that
no adapter callback runs, and check the real platform loader against the in-tree example
module. Compatibility tests assert that major/minor mismatches, undeclared capability
bits and unknown bits are refused with a reason and the exact rejected mask, for both
inspected and registered adapters. These prove ABI linkability and metadata visibility. The App A and App B lifecycle
coverage in §3 is separate from these inspection checks; App B additionally exercises
the host's runtime loader, which keeps the module mapped without registering it. An
out-of-tree fixture installs the SDK into an isolated prefix and builds both independent
applications from outside the repository using only the installed package (P3.9).

## 3. Custom SDK test applications

The requirement is two **independent** applications (BR §5.2). Derived design of
a complementary pair:

| App | Packaging | Coverage focus |
| --- | --- | --- |
| A — processor/chain harness | Static integration compiled into the host | Processor and chain workloads, typed parameters, Basic/Standard validation, reference data, per-block timing |
| B — engine harness | Dynamically loaded shared library | SDK version/capability negotiation, engine workload, internal multithreading, instrumented measurements, Strict validation, out-of-process execution |

**Confirmed:** these two applications remain generic placeholders; no specific
application or DSP library is mandated (BR §21).

**P3.7 App A (Derived design).** The static adapter is compiled into the
`ztick_test_app_a` host executable. Its descriptor publishes all nine lifecycle
callbacks, six typed parameters (boolean, integer, floating-point, enumeration and
string), explicit audio formats and machine-readable unsupported declarations. The
host negotiates SDK version and capabilities, registers the descriptor, creates one
instance per workload, configures typed values, generates deterministic input,
prepares host-generated reference data, runs a warm-up outside the primary interval,
processes per-block timed data for both the processor and chain workloads, then runs
Basic and Standard validation and reports measured, unavailable and not-applicable
metrics. Unit tests cover malformed calls, instance independence, float64/planar
buffers, sink failure and the executable's aggregate result. This satisfies App A's
end-to-end criterion.

**P3.8 App B (Derived design).** The dynamic adapter is compiled as a shared module
that exports the read-only descriptor and all nine lifecycle callbacks but never links
or calls the SDK runtime. Its private host harness first inspects metadata, queries
the SDK version, negotiates the engine capabilities and authorizes first use through
an explicit consent prompter. Only then does a host-owned runtime loader keep the
module mapped, revalidate the loaded descriptor and drive the engine workload through
a selected runner. The harness generates deterministic input and an independent
float32 reference outside the timed DSP interval, runs a discarded warm-up, processes
per-block timed engine work using adapter-owned threads, then runs six Strict checks
and verifies the measured, unavailable, not-applicable and estimated metric statuses.
CTest runs the same harness in and out of process; in child mode the executable loads
and negotiates the module itself and returns only the final status because the fixed
IPC protocol carries no detailed evidence. Unit tests cover runtime-loader failures,
descriptor validity, malformed callbacks, child-mode status and the in-process result.

**P3.9 external-style build (Derived design).** A standalone fixture in
`tests/sdk_external_apps` is a separate CMake project that is never added to the
ztick-bench build. `scripts/check_external_apps.sh` installs the SDK into a fresh
prefix, configures that project with only `CMAKE_PREFIX_PATH`, builds and runs both
applications, and is registered as `integration.external_apps` and
`make external-apps-check`. The static app registers its own descriptor and drives the
processor/chain lifecycle; the dynamic module exports the read-only descriptor without
linking the SDK runtime, and its host loads the module with the platform loader and
drives the engine lifecycle. Both use only the installed public C ABI and no private
ztick-bench headers or targets, so BR §17.8 is verified without modifying ztick-bench.

Both apps must exercise the full adapter contract in
[SDK_SPECIFICATION](SDK_SPECIFICATION.md) and must be buildable without modifying
ztick-bench itself (BR §17.8). They are distinct applications, not two configurations
of one binary, so a shared defect cannot validate both.

The Phase 3 verification pass (ROADMAP P3.10) is an integration test in
`tests/integration/phase3_integrations_test.cpp`. It drives the static processor/chain
harness and the dynamic engine harness end-to-end in one process, checks the App B
child entry point for the isolation path, and derives every expected count and status
from the published app contracts and written validation levels rather than from the
results under test. This demonstrates the §1 exit criterion that both test apps run
end-to-end; the out-of-tree builds in §2 and the component tests in §8 remain separate
evidence for BR §17.8 and isolation.

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
`tests/integration/phase2_engine_test.cpp`. Across four cases it chains
deterministic generation, all three timing levels, validation and the METRICS §3
aggregates, checking each expected value against an independent calculation so
the Phase 2 exit criterion is demonstrated across component boundaries.

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

**P3.5 runner coverage (Derived design).** Unit tests parse every isolation token,
check auto and explicit selection against trust authorization and backend availability,
and assert that the run record carries the selected public execution mode. The
in-process runner is tested with an injected workload for success, status failure and
exception paths, plus refusal paths that prove no workload is invoked without a
selectable in-process mode.

**P3.6 process coverage (Derived design).** Injected launchers verify every
out-of-process status mapping, including child termination, launch failure, malformed
IPC and launcher exceptions, without creating processes. On POSIX systems a test
helper is launched for real: its final status crosses the fixed IPC message, a
signal-terminated child is converted to a workload failure, and a later child still
runs in the same host process (BR §14, ARCHITECTURE §6).

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

**P4.1 config coverage (Derived design).** Unit tests read a complete v1 config,
assert both version fields, write it as compact JSON and compare a second read by
value. They also reject a missing or unsupported `config_version`, a future SDK
minor version, mistyped top-level sections, malformed JSON and non-finite reals,
and check the numeric SDK version compatibility helper directly.

**P4.2 result coverage (Derived design).** Unit tests derive the expected session
status independently for complete, incomplete and failed run sets, verify that a
run with recorded failures is demoted to `incomplete`, and confirm that a failed
finalization leaves the document unchanged. Writer tests assert exact bytes after
create and replace, verify that a failed replacement preserves the target and
leaves no temporary file, and check that invalid documents create no file. A
write/read round trip confirms that the derived statuses survive serialization.

## 10. Acceptance criteria traceability

| BR §17 criterion | Verifying tests |
| --- | --- |
| 1. Linux fully supported | System tests on Linux; platform fields checked for `unsupported`/`unavailable` reporting |
| 2. Two independent SDK test apps | §3 App A built and run (P3.7); App B built and run in both runner modes (P3.8); Phase 3 end-to-end verification pass (P3.10) |
| 3. Static and dynamic integrations | §3 apps A/B; §8 discovery/load tests |
| 4. Initial metrics and workload levels | §4, §5, §6 tests across processor/chain/engine |
| 5. CLI, JSON, comparisons, regressions | §9 tests; §6 comparison tests |
| 6. TUI principal workflows | System tests through dashboard, config, execution monitoring and results screens ([TUI](TUI_SPECIFICATION.md)) |
| 7. Automated correctness/overhead/repeatability/failure/isolation tests | §4–§8 |
| 8. Third-party integration docs | [SDK_SPECIFICATION](SDK_SPECIFICATION.md) conformance checklist and build steps validated by the out-of-tree builds of both apps (P3.9) |

## 11. Derived versus required

Test cases in §2–§9 are derived design implementing REQ-TEST-01–03 and the ledger
requirements they cite. They do not add new v1.0 requirements.
