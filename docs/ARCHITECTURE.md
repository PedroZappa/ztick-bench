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
`-Werror` in CI and ASan/UBSan in debug builds. The tool version is `0.2.0-dev`
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
| Core engine | Validate configuration against capabilities, sequence warm-up/run/validation, aggregate results | BR §3, §10, §13 |
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
| `ztick_results` | JSON serialization | No (CSV may be optional) |
| `ztick_platform` | platform abstraction + Linux implementation | Monitoring implementation is platform-selected |
| `ztick_cli` | core, results | No for CLI users |
| `ztick_tui` | core and results through the C ABI via Rust FFI; Cargo crate driven by CMake | **Yes** — Rust toolchain and ratatui are needed only for TUI builds |
| `ztick_static_adapters` | SDK C ABI | **Yes** |
| `ztick_plugin_host` | SDK C ABI, platform library loader | **Yes** |
| `ztick_runner` | SDK C ABI, IPC transport | **Yes** — needed for out-of-process execution |
| `ztick_test_apps` | SDK C ABI | Yes for installation; required for acceptance testing |

The Phase 3 example targets are `ztick_test_app_static`, which links the compiled
runtime, and `ztick_test_app_dynamic`, which stays header-only so the host owns the
single adapter registry (SDK_SPECIFICATION §10). The static target exposes
`ztick_test_app_static_register` for host initialization (P3.1); its lifecycle
callbacks arrive in P3.7. `ztick_plugin_host` consumes the dynamic example through its
private POSIX loader abstraction (P3.2); on non-POSIX platforms the fallback reports
infrastructure failure until platform work resumes post-v1.0.

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
src/cli/                        CLI command layer
src/tui/                        Rust/ratatui dashboard front end (Cargo crate)
tools/ztick-sysinfo/            Phase 1 developer system-info executable
tests/unit/                     unit tests (Catch2 v3)
tests/integration/              integration tests
tests/system/                   end-to-end system tests
tests/fixtures/                 reference audio/config fixtures (BR §10.2)
examples/static_adapter/        Phase 3 static SDK test app
examples/dynamic_adapter/       Phase 3 dynamic SDK test app
cmake/                          helper modules + ztick-sdk-config.cmake.in
.github/workflows/              CI scaffolding
```

The top-level `include/` directory is the installed include root so third parties
can build adapters with `find_package(ztick-sdk)` without modifying the repository
(BR §17.8). `tools/` keeps the Phase 1 developer executable distinct from the
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
- **Compatibility gate** runs before configuration or execution: it checks the SDK
  version and required capability bits, refuses incompatible or undeclared
  integrations with a reason, and records the outcome for the run record
  (RESULTS_FORMAT §4.3). It is metadata-only and calls no adapter code; detailed
  format/parameter checks arrive with the lifecycle harnesses in P3.7/P3.8
  (SDK_SPECIFICATION §4).
- **Trust manager** compares discovered identity against the persisted trusted
  list and requests confirmation through the active front end before first use.
- **Runner selection** prefers in-process for trusted integrations and
  out-of-process for untrusted ones or where stronger fault isolation is needed;
  the user may also request a mode explicitly. The chosen mode is written to the
  run record.
- **Runner/IPC layer** carries configuration, audio blocks and measurements
  across the process boundary. Its cost is outside the DSP interval where
  practical and is reported separately when it cannot be excluded.
- A plugin marketplace/package manager is out of scope (BR §6.1).

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
the next independent test. Result writes use a temporary file and atomic rename
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

## 8. Comparison and reporting placement

Run-to-run, build-to-build and machine-to-machine comparisons are orchestrated by
the core using stored results; statistical methods are owned by
[METRICS_REFERENCE](METRICS_REFERENCE.md#6-comparison-and-regression-statistics),
and the persisted representation by [RESULTS_FORMAT](RESULTS_FORMAT.md). The
`compare` and `report` CLI commands are specified in
[PROJECT_SPEC §3](PROJECT_SPEC.md#31-in-scope-for-v10) and
[TESTING_STRATEGY](TESTING_STRATEGY.md).

## 9. Decision dependencies

- **Q46 is confirmed:** C++20, CMake and a versioned C ABI are fixed; it is no
  longer a pending dependency. The TUI clause was superseded on 2026-11-22
  (Rust/ratatui; BR §21); the C ABI remains the boundary the TUI calls via FFI.
- **Q47 is deferred:** suites are post-v1.0; no suite orchestration or `suite`
  CLI component is specified, though the config format stays suite-friendly.
- **Q48 is confirmed:** this document set is the adopted layout and the ownership
  map in [PROJECT_SPEC §4](PROJECT_SPEC.md#4-document-ownership-map) is normative.
