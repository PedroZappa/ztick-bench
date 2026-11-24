# Architecture

| | |
| --- | --- |
| **Status** | Technical specification — v1.0 baseline complete |
| **Source of truth** | [DECISIONS.md](DECISIONS.md), especially BR §5, §6, §14, §15, §22 |
| **Authoritative for** | Components, dependency direction, build targets, host-side plugin/trust/isolation runtime |
| **Not authoritative for** | C ABI details ([SDK](SDK_SPECIFICATION.md)), metric formulas ([METRICS](METRICS_REFERENCE.md)), schemas ([RESULTS](RESULTS_FORMAT.md)) |

Labels follow [PROJECT_SPEC §5](PROJECT_SPEC.md#5-normative-language): **Required**
statements trace to the ledger; **Derived design** statements are implementable
detail that may be refined without changing a requirement.

---

## 1. Architectural principles

**REQ-ARCH-01 (Required).** The core is portable and independent of individual DSP
libraries and frameworks. Dependencies are separated by responsibility: core
engine/SDK, TUI, CLI, system monitoring, plugin loading, JSON serialization and
testing. Optional features must not force dependencies on users who only need the
core engine or CLI. *Source: BR §15.*

**REQ-ARCH-02 (Required).** Linux-specific behavior sits behind platform
abstractions so macOS and future Windows support remain feasible. *Source: BR §15.*

**REQ-ARCH-03 (Required).** The offline core must not acquire live-audio or
platform-specific assumptions; simulated real-time and live-audio modes are added
later on the same abstractions. *Source: BR §2.3.*

**REQ-ARCH-04 (Required).** Dependencies are maintained, permissively licensed, and
documented with versions and licenses when introduced. No specific third-party
runtime library is mandated for JSON or IPC, and none was mandated for the TUI at
baseline; ratatui 0.30.2 (MIT, Rust 1.88+) is now the confirmed TUI library
(BR §21, confirmed 2026-11-22). Catch2 v3 is the confirmed **dev-only** C/C++
test framework (BR §22); it is never a runtime or installed dependency.
*Source: BR §15, §21, §22.*

Build system is CMake; the implementation language is C++20 for the core, CLI and
SDK, with the TUI as the sole Rust/ratatui exception (BR §21). A versioned C ABI
is the integration boundary for adapters and for the Rust TUI's FFI calls.
*Source: BR §15; BR §18 Q46, amended 2026-11-20 and 2026-11-22.*

Toolchain baseline is GCC 13+ or Clang 16+ with `-Wall -Wextra -Wpedantic`,
`-Werror` in CI and ASan/UBSan in debug builds. The tool version is `0.3.0-dev`
and the SDK ABI version is `1.0.0`. *Source: BR §21–§22.*

## 2. Component map

```text
  +-------------------+   +-------------------+
  |       CLI         |   |       TUI         |     front ends (BR §12)
  +---------+---------+   +---------+---------+
            |                      |
            +----------+-----------+
                       v
  +---------------------------------------------------------------+
  |                      Core engine                              |
  |  run orchestration, config validation, execution sequencing   |
  +---+-----------+-----------+-----------+-----------+------------+
      |           |           |           |           |
      v           v           v           v           v
 +----------+ +----------+ +----------+ +----------+ +----------+
 | Workload | | Timing & | |Validation| | Results  | | Platform |
 | + input  | | statistics| | engine  | | + config | | monitor  |
 | generators| | collector | |          | | serialize| | abstraction|
 +----------+ +----------+ +----------+ +----------+ +----+-----+
      |                                                    |
      v                                                    v
 +----------------------------------------+        +---------------+
 | Integration / plugin subsystem          |        | OS-specific   |
 | static registry, dynamic loader,       |        | implementations|
 | discovery, trust, execution runner     |        | (Linux first) |
 +-------------------+--------------------+        +---------------+
                     |
                     v
        SDK C ABI  <-------------------->  adapters / DSP libraries
```

| Component | Responsibility | Ledger |
| --- | --- | --- |
| Core engine | Validate configuration against capabilities, sequence warm-up/run/validation, build result documents, aggregate results | BR §3, §10, §13 |
| Workload & generators | Build processor/chain/engine workloads; generate sine, multi-tone, impulse outside the timed interval | BR §4 |
| Timing & statistics | Per-block, whole-run and optional end-to-end timing; aggregate by default; retain raw values and overhead estimates | BR §7 |
| Validation engine | Basic/Standard/Strict checks separate from timing | BR §10 |
| Results & config | Versioned JSON/CSV, session export/import, metadata attachment, comparison data | BR §11–§13 |
| Platform monitor | System info, frequency/thermal/background-load monitoring, affinity, scheduling and priority controls | BR §8, §9 |
| Integration subsystem | Register static adapters, load shared libraries, inspect metadata, enforce trust, choose execution mode | BR §5, §6 |
| CLI / TUI | Command-oriented control and dashboard-first interaction | BR §12 |

Detailed behavior of each component is owned by the documents in the ownership map
of [PROJECT_SPEC §4](PROJECT_SPEC.md#4-document-ownership-map).

## 3. Dependency direction and build targets

**REQ-ARCH-05 (Required).** Dependencies flow from front ends toward the core and
from the core toward abstractions; adapters depend only on the SDK C ABI. DSP
libraries are linked into adapters, never into the portable core. *Source: BR §5.1,
§15 (derived topology).*

Planned targets (Derived design — names are not ledger requirements):

| Target | Depends on | Optional? |
| --- | --- | --- |
| `ztick_sdk` | C ABI headers, compiled runtime symbols, version definitions | No |
| `ztick_core` | SDK headers, workload, timing, statistics, validation, results | No |
| `ztick_results` | JSON serialization, config value tree, portable config and result document schemas, atomic writer, CSV projection and metadata attachment/comparison | No (CSV may be optional) |
| `ztick_platform` | platform abstraction + Linux implementation + inventory-to-metadata adapter; depends on `ztick_results` for the field type | Monitoring implementation is platform-selected |
| `ztick_cli` | core, results, platform; owns command dispatch, help/version, `list`, `run` argument parsing/overrides/file I/O, `system` inventory rendering and stored-result rendering | No for CLI users |
| `ztick-bench` | `ztick_cli`; thin executable that writes dispatch output and returns its exit code | No for CLI users |
| `ztick_tui` | core and results through the C ABI via Rust FFI; Cargo crate driven by CMake | **Yes** — Rust toolchain and ratatui are needed only for TUI builds |
| `ztick_static_adapters` | SDK C ABI | **Yes** |
| `ztick_plugin_host` | SDK C ABI, platform library loader | **Yes** |
| `ztick_runner` | SDK C ABI, IPC transport | **Yes** — needed for out-of-process execution |
| `ztick_test_apps` | SDK C ABI | Yes for installation; required for acceptance testing |

The Phase 3 example targets are `ztick_test_app_static`, which links the compiled
runtime, and `ztick_test_app_dynamic`, which does not link `ztick_sdk` so the host owns
the single adapter registry (SDK_SPECIFICATION §10). The static target exposes
`ztick_test_app_static_register` for host initialization and a complete
processor/chain lifecycle. `ztick_test_app_static_harness` drives that lifecycle from
the `ztick_test_app_a` executable registered with CTest (P3.7). The dynamic module
exports a read-only descriptor with a complete engine lifecycle;
`ztick_test_app_b_harness` drives it from the `ztick_test_app_b` executable in both
in-process and out-of-process CTest runs (P3.8). `ztick_plugin_host` consumes the
dynamic example through its private POSIX loader abstraction (P3.2); on non-POSIX
platforms the fallback reports infrastructure failure until platform work resumes
post-v1.0.

P3.9 adds a separately built fixture in `tests/sdk_external_apps`: an independent
static app and a dynamic module plus host that are configured as their own CMake
project against an installed SDK prefix. They use only the public headers and
`ztick::sdk`, not the in-tree examples or plugin-host internals. This makes the
install/export boundary explicit: `ztick_sdk` is the only exported target, and the
plugin loader, trust store, runner and IPC implementation stay private to
ztick-bench (SDK_SPECIFICATION §13).

### 3.1 Source-tree layout (derived design)

The confirmed repository layout (BR §22) is:

```text
include/ztick/                  installed public C headers (sdk.h, types.h, version.h)
include/ztick/cpp/              installed C++20 convenience wrappers
src/sdk/                        compiled C ABI runtime and private registry observers
src/core/                       engine, workload, timing, statistics, validation
src/platform/include/           platform abstraction headers
src/platform/linux/             Linux implementation
src/plugin/                     discovery, trust, loader, runner/IPC
src/results/                    JSON, CSV, config, comparisons
src/cli/                        CLI command layer and ztick-bench executable
src/tui/                        Rust/ratatui dashboard front end (Cargo crate)
tools/ztick-sysinfo/            Phase 1 developer system-info executable
tests/unit/                     unit tests (Catch2 v3)
tests/integration/              integration tests
tests/system/                   end-to-end system tests
tests/sdk_consumer/             out-of-tree find_package consumer fixture
tests/sdk_external_apps/        out-of-tree static and dynamic app fixture (P3.9)
tests/fixtures/                 reference audio/config fixtures (BR §10.2)
examples/static_adapter/        Phase 3 static SDK test app
examples/dynamic_adapter/       Phase 3 dynamic SDK test app
cmake/                          helper modules + ztick-sdk-config.cmake.in
.github/workflows/              CI scaffolding
```

The top-level `include/` directory is the installed include root so third parties
can build adapters with `find_package(ztick-sdk)` and the exported `ztick::sdk`
target without modifying the repository (BR §17.8, SDK_SPECIFICATION §13). Only
that target and those public headers are installed; private headers under
`src/*/include/` and the plugin-host library are not. `tools/` keeps the Phase 1 developer executable distinct from the
Phase 4 `ztick-bench system` command (BR §21). This layout is consistent with the
header rules in `AGENTS.md`.

## 4. Execution and data flow

**REQ-ARCH-06 (Required).** Input generation and setup occur outside the primary
DSP timing interval; end-to-end timing is an explicit opt-in. *Source: BR §4.2,
§7.4.*

Derived design of the run pipeline:

1. Load and validate the portable config document
   ([RESULTS](RESULTS_FORMAT.md)); resolve profile defaults.
2. Discover/inspect integrations and check SDK version + capabilities
   ([SDK](SDK_SPECIFICATION.md)). Reject unsupported configurations with a reason.
3. Generate input data and allocate audio buffers — **outside** the timed interval.
4. Warm up (if the profile requires it) — **outside** the timed interval.
5. Execute the workload in the selected isolation mode; collect per-block or
   aggregate timings according to measurement detail settings.
6. Stop timing. Run correctness validation as a separate phase.
7. Attach system/build/machine/workload metadata and write results atomically.

**P4.6 implementation (Derived design).** The CLI owns steps 1's file and
override handling and step 7's atomic write. `ztick_core` owns
`run_configured_session`, which consumes the already-parsed config, plans the
semantic execution settings, injects input/setup/warmup/validation actions into
`run_sequence`, computes canonical metrics and returns the result document.
Host services (monotonic clock, machine inventory, session identity and
timestamps) are injected so the core stays independent of the CLI, TUI and OS.
P4.6 executes a deterministic built-in reference workload and records a warning
that no integration plugin was loaded; plugin discovery, trust and mode selection
are separate host subsystems owned by later tasks.

**P4.7 implementation (Derived design).** The CLI collects the static inventory
through `ztick_platform::SystemInfoProvider`, converts it with the existing
inventory-to-metadata adapter and renders each field's availability state and
detail. The CLI does not duplicate the availability mapping in
[PLATFORM §6](PLATFORM_MONITORING.md#6-availability-states), and the Phase 1
`ztick-sysinfo` developer executable remains a separate tool.

Parallel workloads add wall-clock and worker-thread CPU time where available
(BR §7.2). IPC, process management and data movement for out-of-process execution
are kept outside the measured interval wherever practical (BR §6.3).

## 5. Plugin subsystem (host side)

**REQ-ARCH-07 (Required).** Discovery supports explicit paths, a user plugin
directory and a project-local plugin directory. Metadata inspection must be
possible without executing the plugin, and discovery alone must never execute a
plugin. *Source: BR §6.1.*

**REQ-ARCH-08 (Required).** Plugins are explicitly installed or supplied by the
user; identity, version, SDK compatibility and declared capabilities are
displayed; a trusted-plugin list is maintained; first execution of an untrusted
plugin requires explicit user confirmation. *Source: BR §6.2.*

**REQ-ARCH-09 (Required).** Both in-process and out-of-process execution are
supported, the used mode is recorded, and process isolation is not represented as
a complete security sandbox. *Source: BR §6.3.*

Derived design:

- **Discovery module** enumerates the three sources and reads a read-only metadata
  descriptor/manifest so identity and capabilities can be shown without calling
  plugin code. The exact manifest encoding is implementation detail, but the
  no-execution guarantee is required.
  **P3.2 implementation (Derived design):** `ztick_plugin_host` resolves the dynamic
  adapter's `ZTICK_ADAPTER_EXPORT_SYMBOL` data symbol through a private platform
  loader abstraction, validates it with the SDK's shared structural check, copies
  identity into owning storage and releases the library. It never calls a lifecycle
  callback or entry point; directory enumeration and trust decisions remain separate
  host work. Modules without the symbol or with malformed/incompatible metadata are
  rejected with a reason rather than silently skipped (SDK_SPECIFICATION §10).
  **P4.10 implementation (Derived design):** `ztick_plugin_host` adds a
  `discover_plugins` pass that takes three sources (explicit paths, a user directory
  and a project-local directory), enumerates candidates through an injected
  `PluginDirectoryEnumerator` and reuses `inspect_dynamic_adapter` for each path. A
  `PluginSource` token records which source produced a candidate; duplicates keep
  the first source in explicit/user/project order. Missing directories and
  unreadable trust lists are returned as explicit statuses, never omitted. Platform
  path resolution is isolated in `posix/plugin_paths.cpp` (or its unsupported
  stub), so the enumeration policy remains portable and testable.
- **Compatibility gate** runs before configuration or execution: it checks the SDK
  version and required capability bits, refuses incompatible or undeclared
  integrations with a reason, and records the outcome for the run record
  (RESULTS_FORMAT §4.3). It is metadata-only and calls no adapter code. Detailed
  format/parameter checks are performed by the App A static lifecycle harness
  (P3.7) and the dynamic App B harness (P3.8)
  (SDK_SPECIFICATION §4).
- **Trust manager** compares discovered identity against the persisted trusted
  list and requests confirmation through the active front end before first use.
  **P3.4 implementation (Derived design):** `ztick_plugin_host` persists a versioned
  trusted-plugin list behind a `TrustStore` interface (in-memory for tests, atomic
  file replacement for the default XDG location) and asks an injected
  `ConsentPrompter` only for identifiers that are not already trusted. Denial, an
  unavailable front end, malformed identity or a storage failure refuses execution
  with a reason; no plugin module is loaded or called by the trust decision
  (BR §6.2).
- **Runner selection** prefers in-process for trusted integrations and
  out-of-process for untrusted ones or where stronger fault isolation is needed;
  the user may also request a mode explicitly. The chosen mode is written to the
  run record.
  **P3.5 implementation (Derived design):** `ztick_plugin_host` parses the portable
  `execution.isolation` tokens, selects a mode from prior trust authorization and
  backend availability, and copies the public C ABI mode into a `RunExecutionRecord`.
  `InProcessRunner` executes an injected workload on the caller's thread and records
  the selected mode for every workload attempt; a refused selection leaves the mode
  explicitly absent. Selection never loads a module or calls adapter code.
  **P3.6 implementation (Derived design):** `OutOfProcessRunner` launches a prepared
  child command through a platform-selected `ChildProcessLauncher`, waits for it and
  reads one fixed final-status message after the child workload has finished. It maps
  signal termination to a workload failure and launch or protocol failures to
  infrastructure failures, recording the out-of-process mode in every attempt
  (BR §6.3, §14). IPC carries no per-block samples, so process management and
  transport stay outside measured DSP intervals; no timing value is silently adjusted.
- **Runner/IPC layer** carries configuration, audio blocks and measurements
  across the process boundary. Its cost is outside the DSP interval where
  practical and is reported separately when it cannot be excluded.
- A plugin marketplace/package manager is out of scope (BR §6.1).
- **P4.10 CLI presentation (Derived design).** `ztick_cli` owns `plugin list` and
  `plugin inspect`. They consume discovery and trust through injected services,
  render explicit directory, inspection, compatibility and trust states, and build
  the structured payloads for the JSON envelope. Neither command creates an
  adapter, grants trust or prompts: inspection stops at the descriptor data symbol
  (BR §6.1, §6.2). Output-format selection and exit-code ownership are specified in
  [PROJECT_SPEC §3.3](PROJECT_SPEC.md#33-cli-command-set) and the envelope schema in
  [RESULTS_FORMAT §12](RESULTS_FORMAT.md#12-machine-readable-output-envelope).

The integration-side obligations for metadata, versions and capabilities are owned
by [SDK_SPECIFICATION](SDK_SPECIFICATION.md).

## 6. Failure containment

**REQ-ARCH-10 (Required).** Workload failures, infrastructure failures and
measurement warnings are distinguished; completed measurements are preserved;
independent tests continue where safe; a crashing plugin must not bring down the
main TUI or CLI where process isolation is used; incomplete runs are marked as
such; result files are not corrupted. *Source: BR §14.*

Derived design: the out-of-process runner converts a crashed child into a
workload-failure record for that test point, allowing the core to continue with
the next independent test. **P3.6 implementation (Derived design):** signal
termination is detected while reaping the child, before any IPC status is trusted,
and is returned as `ztick_status_processing_failure`; the host process is never
terminated by that signal. Result writes use a temporary file and atomic rename
(encoding in [RESULTS](RESULTS_FORMAT.md)).

## 7. Platform abstraction boundary

The abstraction exposes capability-probed operations for system information,
runtime monitoring, CPU affinity/topology, worker-thread control and scheduling
priority. Each operation reports one of: a supported value, `unavailable` (platform
cannot provide it), `unsupported` (never implemented on this platform/backend), or
`not_applicable` (meaningless for the selected mode). This satisfies the rule that
platform-dependent capabilities must not be silently omitted (BR §17). The mapping
to metric value statuses is owned by
[PLATFORM_MONITORING §6](PLATFORM_MONITORING.md#6-availability-states).

**Metadata bridge (Derived design).** The platform layer converts each inventory
item into the availability-aware metadata field type owned by `ztick_results`, so
OS-specific collection stays in `ztick_platform` and result encoding stays in
`ztick_results`. The field shape, attachment rules and comparability warnings are
owned by [RESULTS_FORMAT §4.4](RESULTS_FORMAT.md#44-metadata-attachment-behavior-derived-design).

## 8. Comparison and reporting placement

Run-to-run, build-to-build and machine-to-machine comparisons are orchestrated by
the core using stored results; statistical methods are owned by
[METRICS_REFERENCE](METRICS_REFERENCE.md#6-comparison-and-regression-statistics),
and the persisted representation by [RESULTS_FORMAT](RESULTS_FORMAT.md). The
`compare` and `report` CLI commands are specified in
[PROJECT_SPEC §3](PROJECT_SPEC.md#31-in-scope-for-v10) and
[TESTING_STRATEGY](TESTING_STRATEGY.md).

**P4.8–P4.9 implementation (Derived design).** The core function
`compare_result_documents` validates two stored results, reuses the metadata
comparison owned by `ztick_results` and returns the persisted raw comparison
shape without writing a file. The P4.9 entry point
`compare_result_documents_with_policy` reuses that record, collects run-level
samples by metric identifier and applies the thresholded bootstrap classification
owned by [METRICS §6](METRICS_REFERENCE.md#6-comparison-and-regression-statistics).
The CLI owns file reads, argument and policy-option validation, and text
rendering, with an injectable reader so both success and failure paths are
testable. The core never writes a comparison file. `--fail-on-regression` is a CLI
flag that maps a stored regression classification to exit code `4` without
recomputing statistics; the JSON envelope is assembled only in `ztick_cli`
(P4.10, [RESULTS_FORMAT §12](RESULTS_FORMAT.md#12-machine-readable-output-envelope)).

## 9. Decision dependencies

- **Q46 is confirmed:** C++20, CMake and a versioned C ABI are fixed; it is no
  longer a pending dependency. The TUI clause was superseded on 2026-11-22
  (Rust/ratatui; BR §21); the C ABI remains the boundary the TUI calls via FFI.
- **Q47 is deferred:** suites are post-v1.0; no suite orchestration or `suite`
  CLI component is specified, though the config format stays suite-friendly.
- **Q48 is confirmed:** this document set is the adopted layout and the ownership
  map in [PROJECT_SPEC §4](PROJECT_SPEC.md#4-document-ownership-map) is normative.
