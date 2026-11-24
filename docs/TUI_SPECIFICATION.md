# TUI Specification

| | |
| --- | --- |
| **Status** | Technical specification — v1.0 baseline complete |
| **Source of truth** | [DECISIONS.md](DECISIONS.md) BR §2.2, §6.2, §8.1, §12.1, §14, §21 (TUI technology) |
| **Authoritative for** | Dashboard and screen-level TUI behavior, interaction rules, run-state presentation |
| **Not authoritative for** | CLI command set ([PROJECT_SPEC §3](PROJECT_SPEC.md#31-in-scope-for-v10)), metric semantics ([METRICS](METRICS_REFERENCE.md)), result schema ([RESULTS](RESULTS_FORMAT.md)) |

---

## 1. Principles

**REQ-TUI-01 (Required).** Provide a modern, dashboard-first TUI, with traditional
menus/configuration/detail screens where useful. *Source: BR §12.1.*

**REQ-TUI-05 (Required).** The TUI is a Rust front end using ratatui 0.30.2 (MIT;
Rust 1.88+, edition 2024, crossterm backend). It reaches core functionality only
through the versioned `ztick_` C ABI via FFI, performs no DSP work of its own, and
is built by an optional Cargo target driven from CMake. The FFI surface is the
private `ztick_frontend` C boundary in
[SDK_SPECIFICATION §14](SDK_SPECIFICATION.md#14-private-front-end-c-boundary-derived-design):
it is not the installed adapter SDK and adds no installed public header. *Source:
BR §21 (TUI-technology confirmation, 2026-11-22); BR §15, §22.*

Derived design principles:

- The dashboard is the entry point: current system state, available integrations,
  last results and active warnings are visible without navigating a menu tree.
- Every action available in the TUI maps to a CLI capability; the TUI never
  applies a setting the CLI cannot express or the config cannot record. For this
  principle, CLI capability includes the documented config-file, JSON/CSV and
  result-document operations behind `run`, `report` and `compare`; named profiles
  and session export/import are persisted as those documents, not as new CLI
  subcommands.
- The TUI never silently changes workload or timing settings. Changes are visible
  in the run configuration before execution and are snapshotted into results.
- The Cargo crate lives in `src/tui/`; an optional CMake target invokes Cargo only
  when the TUI is enabled. Core/CLI-only builds need no Rust toolchain (BR §15).
- Rust bindings mirror the private `ztick_frontend` opaque handles and fixed-width
  types; no C++ types or exceptions cross the boundary. Result documents are
  decoded from the versioned [RESULTS schema](RESULTS_FORMAT.md), never redefined
  in the TUI.
- TUI code uses rustdoc comments, rustfmt and clippy, with `cargo test` joined to
  the verification gate when Phase 5 lands.

## 2. Screens

| Screen | Purpose | Key content |
| --- | --- | --- |
| Dashboard | Orientation and status | System summary, integration count, recent runs, active warnings, current reliability mode |
| System | Environment inspection | System inventory and availability states from [PLATFORM](PLATFORM_MONITORING.md#2-system-information-inventory) |
| Integrations / Plugins | Discovery and trust | Identity, version, SDK compatibility, declared capabilities, trusted state, execution mode preference |
| Workload | Define what runs | Processor/chain/engine selection, generator and fixed parameters ([BENCHMARK](BENCHMARK_SPECIFICATION.md#2-workload-levels)) |
| Configuration | Dimensions and profile | Sample rate, block size, channels, instances, oversampling, profile, duration/iterations/repetitions |
| Measurement & Validation | Detail settings | Timing levels, aggregate/per-block choice, end-to-end opt-in, validation level, tolerances |
| Parallelism & Scheduling | CPU controls | Worker count, affinity, physical/logical selection, isolation request, priority opt-in |
| Execution monitoring | Live run feedback | Progress by test point, current timing level, warnings as they occur, isolation mode in use |
| Results inspection | Detailed review | Metrics with status labels, raw data availability, validation outcomes, warnings/failures, completeness |
| Compare | Comparison workflow | Baseline/candidate selection, thresholds, significance outcome, comparability warnings |
| Report / Export | Output | JSON/CSV export, session export/import |
| Profiles & Settings | Saved configuration | Named profiles, defaults, trusted-plugin list |

Traditional menus and detail views are used where a dashboard cannot show enough
information (BR §12.1).

## 3. Run-state model

Derived design of states the TUI must represent:

| State | Meaning | Presentation rule |
| --- | --- | --- |
| Idle | No run active | Dashboard overview |
| Configuring | Settings being edited | Show effective config and unsupported findings |
| Validating | Capability/config checks in progress | Show which integration/test points cannot run and why |
| Running | Timed execution active | Progress and live warnings |
| Warning | Instability detected | Warn and record per [PLATFORM](PLATFORM_MONITORING.md#5-reliability-modes-at-the-platform-layer); do not reject automatically |
| Failed | Workload or infrastructure failure | Show classification and context; preserve completed measurements |
| Incomplete | Run ended with failures/interruption | Clearly marked incomplete; never rendered as successful complete |
| Complete | All configured tests finished successfully | Full results available |

## 4. Interaction rules

**REQ-TUI-02 (Required).** The results and UI must distinguish process-level
measurements from instrumented DSP/callback measurements and must not present them
as equivalent. *Source: BR §2.2.*

**REQ-TUI-03 (Required).** Display plugin identity, version, SDK compatibility and
declared capabilities; require explicit user confirmation before first execution
of an untrusted plugin. *Source: BR §6.2.*

**REQ-TUI-04 (Required).** Higher scheduling priority requires explicit opt-in with
clear warnings about system responsiveness and measurement bias. *Source: BR §8.1.*

Additional rules (Derived design):

- Show the active execution mode (in-process/out-of-process) in execution and
  results views, and state that process isolation is not a security sandbox.
- Render the four metric statuses visually distinctly: `measured`, `estimated`,
  `unavailable`, `not_applicable`.
- Show `unsupported`/`unavailable` platform fields rather than hiding them.
- Show when raw measurements and overhead estimates are both retained and confirm
  that overhead was not subtracted.
- If a config from another machine contains unsupported capabilities, block the
  run with an explanation instead of adapting it silently
  ([RESULTS §7](RESULTS_FORMAT.md#7-portability-checks)).

## 5. Data binding

The TUI is a front end over the core engine and result documents
([ARCHITECTURE §2](ARCHITECTURE.md#2-component-map)):

- Configuration screen edits produce the portable config document
  ([RESULTS §6](RESULTS_FORMAT.md#6-config-document)).
- Execution monitoring reads copied progress events from the private C boundary;
  it does not implement timing or validation itself.
- Results, compare and report screens read result documents through that
  boundary; they do not recompute metrics except through the core's comparison
  functions.

## 6. Out of scope for this document

- Exact widget library, keybindings and visual theme — implementation choices
  constrained only by maintained, permissively licensed dependencies (BR §15).
- TUI-specific platform support; platform behavior is owned by
  [PLATFORM_MONITORING](PLATFORM_MONITORING.md).
- Dashboard-heavy implementation before the SDK and core engine are validated by
  working custom applications, per BR §16.

## 7. Phase 5 principal workflows (Derived design)

The BR §17.6 phrase "principal workflows" means these six end-to-end paths. They
are the Phase 5 acceptance scope and map to the numbered tasks in
[ROADMAP §2.5](ROADMAP.md#25-phase-5--tui):

| Workflow | Path | Roadmap task |
| --- | --- | --- |
| Orientation | Open dashboard; inspect system inventory, integration count, recent runs, warnings and active run state | P5.2 |
| Configuration | Edit workload, dimensions, measurement, validation, scheduling and named profile settings; save/reload a valid portable config or confirm a blocked unsupported capability | P5.3 |
| Execution monitoring | Start a run; observe state, test-point progress, timing level, warnings and isolation mode; distinguish process-level from instrumented measurements | P5.4 |
| Results inspection | Open a completed or incomplete result; review all four metric statuses, validation, warnings/failures, raw-data availability and retained overhead | P5.5 |
| Comparison and report | Select stored results; view threshold/significance outcome, comparability warnings and report/export views; export/import a session bundle | P5.6 |
| Trust consent | Inspect an untrusted plugin's identity, version, SDK compatibility and capabilities; grant or deny first-run consent before execution | P5.7 |

P5.1 supplies the Rust crate and FFI gate for all six paths, and P5.8 verifies
them. Screens in §2 that are not named in a workflow remain part of the owning
screen's detail view; no separate roadmap task is required for each widget.
