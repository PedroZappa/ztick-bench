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
and clippy; the private C boundary from
[SDK_SPECIFICATION §14](SDK_SPECIFICATION.md#14-private-front-end-c-boundary-derived-design)
has a standalone C11 caller test and a Rust FFI smoke test. P5.0 registers named
CTest entries for the C11 caller, the Rust FFI smoke test, rustfmt and clippy so
they join `make verify`; P5.1 adds the crate's own `cargo test` suites. P5.8 adds
the six principal workflow system
tests from [TUI §7](TUI_SPECIFICATION.md#7-phase-5-principal-workflows). Derived
design: the named CTest entries are the single definition of each Rust gate:
`tui.rust.format` runs `cargo fmt --all -- --check`, `tui.rust.clippy` runs
`cargo clippy --all-targets -- -D warnings` and `tui.rust.test` runs
`cargo test --all-targets`. `tui.ffi.smoke` stays registered as a named subset
gate for the private boundary. `make verify` executes those entries once through
CTest, so the complete Rust target set is gated without a second, drifting Cargo
invocation. Every verification also configures `ZTICK_BUILD_TUI=OFF` and runs
tests without Rust, proving that core/CLI-only users need no Rust toolchain.

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
- P4.9 tests assert independently computed aggregate means and relative change,
  polarity-aware regression and improvement classification, significant
  sub-threshold raw differences, threshold-only within-variability outcomes,
  insufficient-sample and unknown-polarity `not_applicable` states, one-sided and
  two-sided interval decisions, and rejection of invalid policy values. CLI tests
  cover policy-option parsing, classified rendering and option forwarding through
  `execute_compare`; executable checks confirm the defaults render an applied
  threshold and explicit unavailable significance. P4.10 adds a deterministic
  regression fixture that asserts exit code `4` only under
  `--fail-on-regression`, with the same comparison exiting `0` without the flag.
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

**P4.10 discovery coverage (Derived design).** Unit tests inject both a directory
enumerator and a shared-library loader, and a fake descriptor whose lifecycle
callback counts calls. They assert that explicit, user and project candidates are
enumerated in that order, duplicates keep the first source, a missing directory is
reported with a reason, a malformed module is retained with its failure reason, and
the lifecycle callback is never called. Trust-list tests distinguish a readable
list from an explicit store failure, which must never become an empty trusted list.
CLI-level tests cover `plugin list` option parsing (`--path` separated and inline,
duplicates and unknown options), metadata rendering and inspect failure paths; an
executable-level check confirms both commands reach the shell without loading a
plugin (BR §6.1, §6.2).

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

**P4.3 CSV coverage (Derived design).** Tests parse the exported CSV with an
independent line reader and assert that run, metric, value, unit, status and
source cells equal values specified in the source document. They cover the fixed
header and omitted-fields marker, escaping of commas and quotes, unavailable
metrics with an empty value and a retained reason, both row modes including the
aggregate fallback, metric-less runs, invalid-document rejection and atomic file
replacement.

**P4.4 metadata coverage (Derived design).** Unit tests assert that build metadata
contains one record per captured fact, that an empty captured value becomes an
`unavailable` record with a reason, and that the no-argument builder agrees with
the compile-time captured info. They project workload and environment sections
from a config snapshot, verify attachment populates all three sections without
changing the snapshot, and check that invalid metadata is rejected without partial
mutation. Comparability tests cover equal values, differing values, differing
statuses, matching non-value statuses, an undetectable value on one side, and a
round trip through the atomic result writer. A platform-layer test converts an
injected inventory with all four availability states and asserts each field keeps
its state and reason.

**P4.5 CLI coverage (Derived design).** Unit tests call `dispatch` directly for
every exit-code token, global options, `list` subjects, malformed arguments and
each recognized-but-unavailable command, asserting the exact stream carrying each
message. An executable-level test runs the built binary through CMake to prove
that the returned code reaches the shell and that help, version, list, usage-error
and unavailable paths write the expected text (BR §12.2).

**P4.7 CLI coverage (Derived design).** Unit tests inject a deterministic
inventory containing all four availability states and assert that each rendered
line retains its token, value or reason. They cover extra arguments, a missing
collector and each whole-inventory non-value state, plus a production-collector
smoke check. The executable dispatch test confirms that
`system` exits successfully and writes the availability-state header.

**P4.8 CLI coverage (Derived design).** Unit tests check core comparison deltas
against independently computed values, including zero, negative and unavailable
metrics, both unmatched sides, metadata-derived levels and invalid documents.
CLI tests inject a result reader to cover report evidence and stale-status
mismatch, raw comparison rendering, argument errors and read failures, plus
production reads of atomically written files. Executable-level scripts run both
commands on stored results and verify their usage-error codes.

**P4.10 CI coverage (Derived design).** Unit tests parse `--format text|json` in
both argument forms, reject missing, duplicate and unknown values, and assert that
the JSON envelope has stable header fields and wraps success, failure and usage
outcomes with empty stderr. Format-option errors are also enveloped when a complete
`--format json` was requested. Executable-level dispatch checks verify the header
fields for each outcome and the plugin commands; the end-to-end script generates
two results with a large iteration gap and asserts exit code `4` under
`--fail-on-regression` and exit code `0` without it. The envelope schema is owned by
[RESULTS_FORMAT §12](RESULTS_FORMAT.md#12-machine-readable-output-envelope)
(BR §12.2).

**P4.11 verification coverage (Derived design).** One integration suite drives all
four Phase 4 exit-criterion areas together through the documented surfaces, with
expectations taken from the published contracts rather than the implementations.
CLI workflows execute a real config with dotted overrides, then read and compare
the written results and run both metadata-only plugin commands. Portability checks
round-trip a versioned config through the canonical writer, reject incompatible
schema and SDK versions, and verify that host differences produce explicit
comparability warnings while matching explicit `unsupported` states stay
comparable (BR §13). Comparison checks recompute means, absolute delta and
relative change independently from the fixture samples, retain those samples,
keep a sub-threshold change raw, and surface cross-host warnings. CI checks assert
the stable exit-code tokens, both `--format` forms, the fixed six-field envelope
for success, usage, failure and regression outcomes, and that `--fail-on-regression`
gates only the exit code while detection is still reported. The verification also
covers structured `system`, `report` and `compare` data reaching the envelope, a
defect found and fixed during this pass. The suite is registered with CTest
through `ztick_tests` and joins the `make verify` gate.

**P5.0–P5.1 front-end boundary coverage (Derived design).** The C11 caller test
drives every function in [SDK_SPECIFICATION §14](SDK_SPECIFICATION.md#14-private-front-end-c-boundary-derived-design),
asserting status/reason encoding, struct headers, copied strings/events, handle
lifetimes and invalidation of borrowed views. The Rust smoke test calls the same
symbols through FFI and checks that copied data remains valid after callbacks
return. CTest registration proves the boundary is part of `make verify`, while a
separate `ZTICK_BUILD_TUI=OFF` configuration proves core/CLI-only builds need no
Rust. Screen-state tests then cover each of the six workflows in
[TUI §7](TUI_SPECIFICATION.md#7-phase-5-principal-workflows), including explicit
consent denial, blocked cross-machine capabilities and incomplete-result status.

**P5.4 execution-monitoring coverage (Derived design).** Core tests observe the
validating, per-repetition running, warning and complete phases from an injected
`ProgressObserver`, assert the copied timing/isolation tokens and repetition
context, and verify that a missing injected service emits one failed observation
with its reason. CLI tests inject the same observer through
`dispatch_with_context`, prove that a `run` reaches the core while read-only
commands produce no synthetic progress, and re-check the written result through
the normal dispatch path. The C11 and Rust boundary tests cover `run_start` and
`run_result` error paths, pending availability, copied progress context and the
published JSON envelope, including destruction while a worker may still be
active. Rust app/UI tests drive the execution key flow and assert that the
screen shows run state, config path, test point, timing level, isolation mode,
warnings and result status while keeping the two measurement classes visually
separate. No metric value is invented from a progress event.

**P5.5 results-inspection coverage (Derived design).** Rust data tests decode a
deterministic stored result document containing `measured`, `estimated`,
`unavailable` and `not_applicable` metrics, validation outcomes, classified
warnings/failures, retained raw sample counts and a separate overhead estimate.
They assert that a stored `complete` token is demoted by run failures, failed
validation checks or session failures, and that malformed, unsupported and
minimal documents keep explicit status/error evidence. UI tests assert that all
four statuses, validation, warnings/failures, raw-data availability and retained
overhead are visible, process-level and instrumented measurements remain
distinct, and incomplete evidence is never rendered as complete. Key-flow tests
map `0` to the results screen, load the stored document from the published run
envelope through the safe `result_read` wrapper, and report unreadable paths
without showing a completed result. The C11 and Rust boundary suites already
cover `ztick_frontend_result_read`; the safe wrapper adds an owned-path error
test and reads a real run document in the async run test.

## 10. Acceptance criteria traceability

| BR §17 criterion | Verifying tests |
| --- | --- |
| 1. Linux fully supported | System tests on Linux; platform fields checked for `unsupported`/`unavailable` reporting |
| 2. Two independent SDK test apps | §3 App A built and run (P3.7); App B built and run in both runner modes (P3.8); Phase 3 end-to-end verification pass (P3.10) |
| 3. Static and dynamic integrations | §3 apps A/B; §8 discovery/load tests |
| 4. Initial metrics and workload levels | §4, §5, §6 tests across processor/chain/engine |
| 5. CLI, JSON, comparisons, regressions | §9 tests; §6 comparison tests; Phase 4 end-to-end verification pass (P4.11) |
| 6. TUI principal workflows | P5.8 system tests for the six workflows in [TUI §7](TUI_SPECIFICATION.md#7-phase-5-principal-workflows): orientation, configuration, execution monitoring, results inspection, comparison/report and trust consent; P5.1 C11/Rust FFI smoke tests cover the private boundary |
| 7. Automated correctness/overhead/repeatability/failure/isolation tests | §4–§8 |
| 8. Third-party integration docs | [SDK_SPECIFICATION](SDK_SPECIFICATION.md) conformance checklist and build steps validated by the out-of-tree builds of both apps (P3.9) |

## 11. Derived versus required

Test cases in §2–§9 are derived design implementing REQ-TEST-01–03 and the ledger
requirements they cite. They do not add new v1.0 requirements.
