# Roadmap

| | |
| --- | --- |
| **Status** | Technical specification — v1.0 baseline complete |
| **Source of truth** | [DECISIONS.md](DECISIONS.md) BR §1, §2.3, §12.3, §16, §19, §21 |
| **Authoritative for** | Phase ordering, sequencing rationale, deferred work, decision dependencies |
| **Not authoritative for** | v1.0 scope detail ([PROJECT_SPEC](PROJECT_SPEC.md)), test cases ([TESTING_STRATEGY](TESTING_STRATEGY.md)) |

---

## 1. Accepted roadmap

The phase order below is the accepted roadmap from BR §16. Deliverables and exit
criteria are **planning guidance only**, not binding acceptance gates; only the
BR §16 phase order and BR §17 acceptance criteria bind. The roadmap is
dependency-ordered: no dates or estimates are included (BR §21).

| # | Phase | Deliverables (derived) | Exit criterion (derived) |
| --- | --- | --- | --- |
| 1 | Foundation | Repository, CMake, SDK headers and core types, system information abstraction, `ztick-sysinfo` dev tool | Core and platform abstraction build; `ztick-sysinfo` prints system info with availability states |
| 2 | Benchmark engine | Workloads, input generators, execution, timing, statistics, validation | Generators and timing levels work; metrics computed per [METRICS](METRICS_REFERENCE.md) |
| 3 | Integrations | Static and dynamic adapters, two custom SDK test applications | Both test apps run end-to-end ([TESTING_STRATEGY §3](TESTING_STRATEGY.md#3-custom-sdk-test-applications)) |
| 4 | CLI and results | Profiles, JSON/CSV, comparison, regression detection, CI support | CLI workflows, portable config, comparisons and CI exit codes verified |
| 5 | TUI | Dashboard, configuration, execution monitoring, result inspection | Principal TUI workflows verified |
| 6 | Reliability | Stress tests, isolation, overhead validation, repeatability | Overhead, repeatability, failure and isolation tests pass |
| 7 | Platform expansion | macOS support, subsequent Windows preparation | Platform abstraction extended; unsupported capabilities reported |

**REQ-ROAD-01 (Required).** Validate the SDK and core engine through working custom
applications before investing heavily in the TUI. *Source: BR §16.*

**REQ-ROAD-02 (Confirmed).** v1.0 comprises phases 1–6; phase 7 (macOS support and
Windows preparation) is post-v1.0. *Source: BR §21.*

**REQ-ROAD-03 (Confirmed).** The deliverables, exit criteria and step breakdown in
§1–§2 are planning guidance, not binding acceptance gates; only BR §16 ordering
and BR §17 criteria are binding. *Source: BR §21.*

**REQ-ROAD-04 (Confirmed).** The roadmap is dependency-ordered only; no dates or
estimates are added. *Source: BR §21.*

This is why Phase 3 precedes Phase 5: a dashboard cannot meaningfully validate an
unproven adapter contract.

## 2. Incremental step breakdown (planning guidance)

Each phase is divided below into small, independently completable increments. The
steps are **derived planning guidance only** (REQ-ROAD-03): they do not add v1.0
requirements, and they may be split, merged or reordered within a phase when
dependencies allow. Phase order itself remains binding per BR §16. Each item is
intended to be one reviewable change, with a verifiable completion condition.
Technical detail stays in the owning document named in the
[PROJECT_SPEC ownership map](PROJECT_SPEC.md#4-document-ownership-map); the lists
only state the increment and its completion condition.

### 2.1 Phase 1 — Foundation

- [x] **P1.1 — Bootstrap the root CMake project: C++20, version `0.1.0-dev`, toolchain flags, CTest enabled.** Done when: Configure and build succeed on Linux with GCC 13+ or Clang 16+.
- [x] **P1.2 — Create the source-tree skeleton and per-directory targets from [ARCHITECTURE §3.1](ARCHITECTURE.md#31-source-tree-layout-derived-design).** Done when: Layout matches the frozen tree; every declared target builds from a placeholder source.
- [x] **P1.3 — Add `include/ztick/version.h` with SDK ABI `1.0.0` macros.** Done when: Header compiles standalone as C11 and C++20; values asserted by a test.
- [x] **P1.4 — Add `include/ztick/types.h` with the frozen [core-type inventory](SDK_SPECIFICATION.md#12-core-type-inventory).** Done when: Every listed type is defined once; C11 and C++20 compile checks pass.
- [x] **P1.5 — Add `include/ztick/sdk.h` with opaque handles, `ZTICK_API` and the C-only surface.** Done when: Header is self-contained C11; no C++ types, exceptions or ABI leak.
- [x] **P1.6 — Add C++20 wrapper scaffolding under `include/ztick/cpp/`.** Done when: Wrappers include the C headers and compile in namespace `ztick::sdk`.
- [x] **P1.7 — Define the `ztick_sdk` target, install rules and `cmake/ztick-sdk-config.cmake.in`.** Done when: A scratch consumer outside the repo can `find_package(ztick-sdk)` and include headers (BR §17.8).
- [x] **P1.8 — Integrate Catch2 v3 (pinned, FetchContent fallback) and register the first CTest.** Done when: `ctest --test-dir build --output-on-failure` passes.
- [x] **P1.9 — Unit-test core types and version macros.** Done when: Statuses, availability states and version constants are covered.
- [x] **P1.10 — Define the platform abstraction interface in `src/platform/include/`.** Done when: Interface compiles; operations expose explicit availability states (ARCHITECTURE §7).
- [x] **P1.11 — Implement Linux system-info collection in `src/platform/linux/`.** Done when: Every [PLATFORM_MONITORING §2](PLATFORM_MONITORING.md#2-system-information-inventory) field yields a value or an explicit state.
- [x] **P1.12 — Test missing and unsupported platform-data paths.** Done when: All availability states are produced by tests; no field is silently omitted.
- [x] **P1.13 — Add `tools/ztick-sysinfo/` printing the system-info inventory.** Done when: Running the tool prints every inventory field with its availability state.
- [x] **P1.14 — Phase 1 verification pass.** Done when: The §1 exit criterion is demonstrated with CI flags active.

### 2.2 Phase 2 — Benchmark engine

- [x] **P2.1 — Model processor, chain and engine workloads and profile settings.** Done when: Model round-trips from config-shaped data with unit tests.
- [x] **P2.2 — Implement sine, multi-tone/harmonic and impulse generators.** Done when: Outputs match the fixed definitions in [BENCHMARK §3](BENCHMARK_SPECIFICATION.md#3-built-in-input-generators) and reproducibility tests pass (BR §4.3).
- [x] **P2.3 — Implement execution sequencing with generation, setup and warm-up outside the primary interval.** Done when: A test asserts no measurement is recorded for those phases.
- [x] **P2.4 — Implement per-block, whole-run and opt-in end-to-end timing.** Done when: All three levels are selectable; raw samples are retained.
- [x] **P2.5 — Record overhead estimates separately from raw timings.** Done when: No subtraction occurs before reporting; estimates are independently visible.
- [x] **P2.6 — Implement statistics: mean, median, percentiles, standard deviation, throughput, RTF and scaling efficiency.** Done when: Values match independent reference calculations ([METRICS](METRICS_REFERENCE.md#3-definitions-and-formulas)).
- [x] **P2.7 — Implement Basic/Standard/Strict validation as a separate phase.** Done when: All levels are exercised and timing results are unchanged.
- [x] **P2.8 — Phase 2 verification pass.** Done when: Generators and timing levels work; metrics compute per [METRICS](METRICS_REFERENCE.md).

### 2.3 Phase 3 — Integrations

**Entry state (2026-11-23).** The blocking SDK runtime gap recorded in §4 is closed:
`ztick_sdk` is now a compiled library defining `ztick_sdk_get_version`,
`ztick_sdk_is_compatible`, `ztick_register_adapter` and `ztick_status_message`.
The public `ZTICK_STRUCT_LAYOUT_VERSION` macro tells third parties what to put in a
struct's `version` field. Catch2 unit tests, a standalone C11 link test, the
external-style consumer check and a dynamic-module symbol check exercise the runtime;
the plugin host and the static example link the compiled library. The dynamic example
does not link `ztick_sdk` so the host owns a single registry. P3.1–P3.10 are
complete and Phase 3 is verified. P3.10 bumped the development version to
`0.3.0-dev` per BR §21.

**Progress (after P3.10).** The static App A adapter registers and is discoverable in
process through the private host registry observers, and its executable host harness
drives the complete processor/chain lifecycle end-to-end. The dynamic loader resolves
the exported read-only descriptor as a data symbol, copies identity metadata into
owning storage and calls no plugin code (BR §6.1). A
metadata-only compatibility gate refuses incompatible SDK versions and undeclared
or unknown required capabilities with a reason before an integration is used. A
persisted trusted-plugin list and first-run consent gate refuse an untrusted plugin
unless the user explicitly approves it; denial, absent consent and storage failures
never authorize execution (BR §6.2). Runner selection is now explicit and records
both selected modes in the run-record execution section. A child-process launcher
with fixed final-status IPC now contains signal-terminated workloads and reports
launch, protocol and crash failures without terminating the host (BR §6.3, §14).
The dynamic App B module exposes the full engine lifecycle without linking the SDK
runtime; its host harness negotiates identity and capabilities, authorizes first use,
keeps the module mapped through execution, runs independently generated reference
data and Strict checks, and verifies both in-process and out-of-process CTest runs.
A standalone fixture now installs the SDK and builds two independent applications
from outside the repository using only `find_package(ztick-sdk)` and the public C
ABI: a static processor/chain app and a dynamic engine module plus its host. The
check is wired into CTest and `make external-apps-check`, so BR §17.8 is verified
without modifying ztick-bench. The Phase 3 verification pass drives both apps
end-to-end with expectations derived from their published contracts, covers the
isolated child path, and is registered with CTest
([TESTING_STRATEGY §3](TESTING_STRATEGY.md#3-custom-sdk-test-applications)). P3.9
and P3.10 are complete.

- [x] **P3.1 — Static adapter registry and one minimal static adapter.** Done when: The adapter registers and is discoverable (verified through the private host registry observers).
- [x] **P3.2 — Dynamic loader with metadata-only inspection.** Done when: Metadata is readable without executing plugin code (BR §6.1).
- [x] **P3.3 — SDK version and capability negotiation before use.** Done when: Incompatible integrations are refused with a reason.
- [x] **P3.4 — Trust list and first-run confirmation flow.** Done when: An untrusted plugin requires explicit consent (BR §6.2).
- [x] **P3.5 — In-process runner.** Done when: Mode is selectable and recorded in the run record.
- [x] **P3.6 — Out-of-process runner and IPC.** Done when: A child crash is contained; transport cost is outside the DSP interval (BR §6.3).
- [x] **P3.7 — Custom test app A — static processor/chain harness.** Done when: App A runs end-to-end ([TESTING_STRATEGY §3](TESTING_STRATEGY.md#3-custom-sdk-test-applications)).
- [x] **P3.8 — Custom test app B — dynamic engine harness.** Done when: App B runs end-to-end.
- [x] **P3.9 — External-style build of both apps.** Done when: BR §17.8 is verified without modifying ztick-bench ([TESTING_STRATEGY §10](TESTING_STRATEGY.md#10-acceptance-criteria-traceability)).
- [x] **P3.10 — Phase 3 verification pass.** Done when: Both test apps run end-to-end ([TESTING_STRATEGY §3](TESTING_STRATEGY.md#3-custom-sdk-test-applications)).

### 2.4 Phase 4 — CLI and results

- [x] **P4.1 — Config document schema and parser.** Done when: Round-trip test passes; version field is present ([RESULTS §6](RESULTS_FORMAT.md#6-config-document)).
- [x] **P4.2 — Result document schema and atomic writer.** Done when: Incomplete runs are marked; writes use temporary file plus rename.
- [x] **P4.3 — CSV projection.** Done when: Shared fields agree between JSON and CSV.
- [x] **P4.4 — Build, machine and workload metadata attachment.** Done when: Metadata is present; comparability warnings fire on mismatch.
- [x] **P4.5 — CLI skeleton and `list`.** Done when: Command dispatch and documented exit codes work.
- [x] **P4.6 — `run` command with config files and overrides.** Done when: Runs execute with overrides applied.
- [x] **P4.7 — `system` command using the platform abstraction.** Done when: It prints availability states (BR §22).
- [x] **P4.8 — `report` and `compare` commands.** Done when: Stored results render through both commands.
- [x] **P4.9 — Regression thresholds and significance checks.** Done when: Sub-threshold or normal-variability differences are not flagged.
- [x] **P4.10 — `plugin list` / `plugin inspect`, CI exit codes, machine-readable output.** Done when: CI-oriented tests pass.
- [ ] **P4.11 — Phase 4 verification pass.** Done when: CLI workflows, config portability, comparisons and CI support are verified.

### 2.5 Phase 5 — TUI

- [ ] **P5.0 — Rust/ratatui crate, C ABI FFI bindings and optional Cargo build.**
      Done when: `src/tui/` builds through the optional CMake target with ratatui
      0.30.2 pinned, a C ABI FFI smoke test passes, and core/CLI builds still need
      no Rust toolchain ([TUI §1](TUI_SPECIFICATION.md#1-principles), BR §21).
- [ ] **P5.1 — Dashboard screen wired to core and results.** Done when: Principal run status is visible ([TUI](TUI_SPECIFICATION.md)).
- [ ] **P5.2 — Configuration screen.** Done when: Profiles and dimensions are editable from the TUI.
- [ ] **P5.3 — Execution monitoring screen.** Done when: Live progress and status are shown.
- [ ] **P5.4 — Results inspection screen.** Done when: Metrics and their statuses are displayed.
- [ ] **P5.5 — Comparison and report screens.** Done when: Stored comparisons are viewable.
- [ ] **P5.6 — Trust confirmation interaction.** Done when: First-run consent works through the active front end.
- [ ] **P5.7 — Phase 5 verification pass.** Done when: Principal TUI workflows are verified.

### 2.6 Phase 6 — Reliability

- [ ] **P6.1 — Stress and repeatability runs.** Done when: Variability is established and recorded.
- [ ] **P6.2 — Overhead validation at small block sizes.** Done when: Overhead estimates are reported (BR §7.3).
- [ ] **P6.3 — Failure-injection tests.** Done when: Classification, context and continuation are verified (BR §14).
- [ ] **P6.4 — Plugin isolation and crash tests.** Done when: The host survives a child crash (ARCHITECTURE §6).
- [ ] **P6.5 — Platform warning paths for thermal, frequency and load.** Done when: Warnings are recorded while measurements are retained (BR §9).
- [ ] **P6.6 — Full BR §17 acceptance traceability run.** Done when: All eight criteria verified per [TESTING_STRATEGY §10](TESTING_STRATEGY.md#10-acceptance-criteria-traceability).
- [ ] **P6.7 — v1.0 verification pass.** Done when: Phases 1–6 complete; §6 exit criteria met.

### 2.7 Phase 7 — Platform expansion (post-v1.0, REQ-ROAD-02)

- [ ] **P7.1 — macOS implementation of the platform abstraction.** Done when: macOS builds; unavailable fields are reported as such.
- [ ] **P7.2 — Windows preparation.** Done when: The abstraction accepts a Windows backend without core changes.
- [ ] **P7.3 — Cross-platform verification.** Done when: Portability tests pass on each available platform.

## 3. Mode rollout

BR §2.3 defines the order:

1. **Offline DSP benchmarks** — first implementation priority (Phases 2–6).
2. **Simulated real-time benchmarks** — later.
3. **Live audio benchmarks** — later.

The architecture must accommodate later modes without putting live-audio or
platform-specific assumptions into the offline core (BR §2.3,
REQ-ARCH-03).

## 4. Deferred and future work

Recorded separately from v1.0 requirements per BR §19.

| Item | Source | Trigger / notes |
| --- | --- | --- |
| Simulated real-time mode | BR §2.3, §16 | Requires deadline model and deadline-miss metric activation |
| Live audio mode | BR §2.3, §16 | Requires audio backend/device integration behind platform abstraction |
| White/pink noise, sweeps, silence, musical material, custom audio files | BR §4.2 | Randomized generators need configurable seeds (BR §4.3) |
| REAPER, Ardour, Bitwig Studio, Pure Data, SuperCollider integrations | BR §5.2 | Future possibilities, not first-release dependencies |
| Plugin marketplace / online package manager | BR §6.1 | Explicitly not required for v1.0 |
| SQLite historical storage/querying | BR §12.3 | Later local storage layer |
| macOS support | BR §1, §16 | Phase 7 |
| Windows preparation | BR §1, §16 | Phase 7 and beyond; includes choosing static/shared SDK export decoration |
| Benchmark suites (`suite run`) | BR §18 Q47, §21 | **Deferred 2026-11-20** — post-v1.0; config format kept suite-friendly |
| SDK runtime symbols — `ztick_sdk_get_version`, `ztick_sdk_is_compatible`, `ztick_register_adapter`, `ztick_status_message` | SDK_SPECIFICATION §2, §7 | **Resolved 2026-11-23:** compiled into `ztick_sdk` with unit, C11 and external-consumer link tests, a public `ZTICK_STRUCT_LAYOUT_VERSION` macro and a host-owned-registry symbol guard (see the Phase 3 entry state above). Retained to record the former Phase 3 blocker |
| Generator validation in workload configs; timing-level token validation in profiles | BENCHMARK §2, §5 | Phase 2 follow-up: `workload_from_config` copies raw generator data without calling `generator_from_config`; `profile_from_config` accepts unknown `timing_levels` tokens |
| Per-level overhead percentage basis | METRICS §3 (MET-17) | Phase 2 follow-up: the single `OverheadEstimate` mixes bracketed intervals but uses only the whole-run DSP denominator; define per-level records or a matching interval set before results encoding (Phase 4) |

## 5. Decision dependencies

| Decision | Affects | Status |
| --- | --- | --- |
| Q46 — language standard | Phase 1 build/toolchain, SDK wrappers, all C++ code | **Confirmed 2026-11-20:** C++20 + CMake + versioned C ABI; TUI clause superseded 2026-11-22 (BR §21) |
| TUI technology | Phase 5 build, toolchain, SDK FFI boundary | **Confirmed 2026-11-22:** Rust/ratatui 0.30.2, C ABI via FFI, optional Cargo via CMake |
| Q47 — benchmark suites | Phase 4 CLI/config scope, acceptance criteria | **Deferred 2026-11-20:** not in v1.0; config kept suite-friendly |
| Q48 — documentation layout | This document set and ownership map | **Confirmed 2026-11-20:** multi-file layout adopted |

See [PROJECT_SPEC §7](PROJECT_SPEC.md#7-decision-register) for the full register.
All decisions are resolved; any future change to a confirmed decision requires
explicit approval (BR §19).

## 6. v1.0 exit criteria

v1.0 ends after phase 6 (REQ-ROAD-02). Completion is defined by BR §17 and traced
to tests in
[TESTING_STRATEGY §10](TESTING_STRATEGY.md#10-acceptance-criteria-traceability).
Platform-dependent capabilities must be reported as unsupported or unavailable
rather than silently omitted (BR §17).
