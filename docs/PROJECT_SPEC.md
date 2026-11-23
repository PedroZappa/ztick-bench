# Project Specification (top level)

| | |
| --- | --- |
| **Status** | Technical specification — v1.0 baseline complete |
| **Source of truth** | [DECISIONS.md](DECISIONS.md) (Baseline Record, `BR §n`, including §21–§22) |
| **Authoritative for** | Scope, v1.0 boundary, document ownership, norm language, decision register |
| **Not authoritative for** | Architecture, SDK ABI, metrics, UI, schemas — see ownership map below |

---

## 1. Purpose and identity

**REQ-PROJ-01 (Required).** `ztick-bench` is a standalone, general-purpose
audio/DSP benchmarking application, independent of ALIENMETAL, with its own
development rules defined in `AGENTS.md`. *Source: BR §1; BR §22 supersedes the
former ALIENMETAL-standards clause.*

**REQ-PROJ-02 (Required).** The core must not be coupled to one DSP library, audio
framework, product, or application. *Source: BR §1, §15.*

**REQ-PROJ-03 (Required).** Initial focus is offline DSP benchmarking. Simulated
real-time and live-audio benchmarking are roadmap items, and the architecture must
accommodate them without forcing live-audio or platform-specific assumptions into
the offline core. *Source: BR §1, §2.3.*

Platform order is **Linux first, macOS second, Windows later**. *Source: BR §1.*

**REQ-PROJ-09 (Confirmed).** C++20 is used for the core engine, CLI and SDK
convenience wrappers; the integration boundary is a versioned C-compatible ABI;
CMake is the build system. The TUI is implemented in Rust with ratatui, calling
that C ABI via FFI; this is the sole exception to Q46 (*Source: BR §18 Q46,
confirmed 2026-11-20 and amended 2026-11-22; BR §21*).

## 2. Benchmark targets

Two target types are in scope: DSP libraries through direct integrations/adapters,
and audio applications through process monitoring plus optional application-specific
instrumentation. *Source: BR §2.1.*

Two measurement levels exist and must remain distinguishable in both results and UI:

- **Black-box monitoring** — process-level observation without modifying the
  application (CPU, memory, threads, system metrics).
- **Instrumented monitoring** — SDK-based exposure of DSP/audio timing where the
  application can provide it.

They must never be presented as equivalent. *Source: BR §2.2.*

> Execution detail is owned by [BENCHMARK_SPECIFICATION](BENCHMARK_SPECIFICATION.md);
> metric semantics by [METRICS_REFERENCE](METRICS_REFERENCE.md).

## 3. v1.0 scope boundary

### 3.1 In scope for v1.0

| Area | Required capability | Ledger |
| --- | --- | --- |
| Platforms | Linux fully supported; other platform capabilities reported as unsupported/unavailable | BR §1, §17 |
| Workloads | Individual processors, processing chains, complete DSP engines | BR §4.1 |
| Generators | Sine, multi-tone/harmonic, impulse | BR §4.2 |
| SDK | Static + dynamic integrations; versioned C ABI; C++ wrappers; capability checks | BR §5.1 |
| Validation apps | At least two independent custom test applications | BR §5.2, §17 |
| Plugins | Explicit paths, user/project directories, metadata-only inspection, trusted list, first-run confirmation | BR §6.1–6.2 |
| Isolation | In-process and out-of-process execution, with mode recorded | BR §6.3 |
| Metrics | All categories in BR §7.1, each with measured/estimated/unavailable/N-A status | BR §7.1 |
| Timing | Per-block, whole-run, optional end-to-end; DSP-only primary boundary | BR §7.2–7.4 |
| Parallelism | Independent instances and engine-internal multithreading; affinity/worker/priority controls | BR §8 |
| Environment | System info, frequency/temperature/throttling monitoring, warn-not-reject policy | BR §9 |
| Validation | Basic/Standard/Strict levels, separated from timing | BR §10 |
| Comparisons | Run-to-run, build-to-build, machine-to-machine; thresholded significance-tested regressions | BR §11 |
| Interfaces | Dashboard-first TUI and full command CLI with config files, overrides, CI exit codes, machine-readable output | BR §12 |
| Results | Versioned JSON canonical, CSV, session export/import, attached metadata | BR §12.3, §13 |
| Integrity | Failure classification, atomic result writes, incomplete-run marking | BR §14 |
| Build/tests | CMake, separated dependencies, automated tests per §17 | BR §15–§17 |

**REQ-PROJ-10 (Confirmed).** Roadmap phases 1–6 constitute v1.0; phase 7
(macOS support and Windows preparation) is post-v1.0. *Source: BR §21.*

### 3.2 Explicitly deferred (future, not v1.0 requirements)

Collected here so they are not mistaken for requirements — per BR §19.

| Deferred item | Ledger |
| --- | --- |
| Simulated real-time benchmarking; live-audio benchmarking | BR §1, §2.3, §16 |
| Noise (white/pink), frequency sweeps, silence, musical material, custom audio file generators | BR §4.2 |
| Integrations for REAPER, Ardour, Bitwig Studio, Pure Data, SuperCollider and other applications | BR §5.2 |
| Plugin marketplace / online package manager | BR §6.1 |
| SQLite historical storage and querying | BR §12.3 |
| macOS support, Windows preparation | BR §1, §16 |
| Benchmark suites (`suite run`) — **deferred 2026-11-20 (Q47)**; config format kept suite-friendly | BR §18 Q47, §21 |

### 3.3 CLI command set

**REQ-PROJ-12 (Required).** The initial CLI command structure is:

```bash
ztick-bench system
ztick-bench list
ztick-bench run
ztick-bench compare
ztick-bench report
ztick-bench plugin list
ztick-bench plugin inspect
```

The CLI supports configuration files, command-line overrides, CI-friendly exit
codes and machine-readable output. *Source: BR §12.2.*

**Derived design (P4.5).** Argument routing lives in the `ztick_cli` module, which
returns output text, error text and a stable exit code; the `ztick-bench`
executable only writes those streams and returns the code. The CI contract is:

| Code | Token | Meaning |
| --- | --- | --- |
| `0` | `success` | The command completed as requested. |
| `1` | `usage_error` | Unknown command or option, extra arguments or malformed subcommand. |
| `2` | `unavailable` | Recognized command whose implementation has not landed; help text lists the full command set. |
| `3` | `failure` | The command ran but hit a runtime or infrastructure failure. |

`--help` (`-h`) and `--version` are global options. `list` and `list profiles`
list the built-in profiles from [BENCHMARK §5](BENCHMARK_SPECIFICATION.md#5-profiles).
Other documented commands are recognized and return `unavailable` until their
roadmap tasks land. Machine-readable output formats for data-bearing commands are
owned by the later command tasks (BR §12.2).

**Derived design (P4.6).** `run` takes one positional config path followed by
`--output <path>` or `--output=<path>` and repeated `--set path=value` or
`--set=path=value` assignments. Dotted paths traverse existing config objects and
may create only the final member; an invalid assignment leaves the config
unchanged. A value starting with `"`, `{` or `[` is JSON; otherwise the existing
member type selects integer, real, boolean or string interpretation. The default
result path replaces the config extension with `.result.json`. Core-owned
orchestration validates the overridden snapshot, executes the configured
repetitions and builds the result document; the CLI writes it atomically
([ARCHITECTURE §4](ARCHITECTURE.md#4-execution-and-data-flow),
[RESULTS §6](RESULTS_FORMAT.md#6-config-document)). P4.6 uses the deterministic
built-in reference workload and records a warning that no integration plugin was
loaded; plugin execution is owned by later roadmap tasks. Invalid overrides are
`usage_error`; unreadable configs, semantic failures and write failures are
`failure`.

## 4. Document ownership map

| Topic | Owner | Ledger source |
| --- | --- | --- |
| Scope, targets, v1.0 boundary, decision register | [PROJECT_SPEC.md](PROJECT_SPEC.md) | BR §1–§3, §18, §21 |
| Components, dependency direction, build targets, host plugin/trust runtime | [ARCHITECTURE.md](ARCHITECTURE.md) | BR §5, §6, §14, §15 |
| C ABI, C++ wrappers, adapter contract, version/capability checks | [SDK_SPECIFICATION.md](SDK_SPECIFICATION.md) | BR §5 |
| Workloads, generators, profiles, execution lifecycle, validation behavior | [BENCHMARK_SPECIFICATION.md](BENCHMARK_SPECIFICATION.md) | BR §2–§4, §8, §10 |
| Metric definitions, formulas, statuses, regression statistics | [METRICS_REFERENCE.md](METRICS_REFERENCE.md) | BR §7, §11.3 |
| TUI screens and interaction | [TUI_SPECIFICATION.md](TUI_SPECIFICATION.md) | BR §12.1 |
| System info, monitoring, affinity, scheduling, priority | [PLATFORM_MONITORING.md](PLATFORM_MONITORING.md) | BR §8, §9 |
| JSON/CSV schemas, config format, metadata, portability, failure encoding | [RESULTS_FORMAT.md](RESULTS_FORMAT.md) | BR §11–§14 |
| Test strategy and acceptance traceability | [TESTING_STRATEGY.md](TESTING_STRATEGY.md) | BR §5.2, §14, §16–§17 |
| Phase ordering, deferred work, decision dependencies | [ROADMAP.md](ROADMAP.md) | BR §1, §2.3, §12.3, §16, §19 |
| Terminology | [GLOSSARY.md](GLOSSARY.md) | all |

## 5. Normative language

- **Required / Must / Shall** — a confirmed requirement from the ledger.
- **Expected** — ledger guidance written as "should"; a strong expectation, not an
  absolute mandate.
- **Derived design** — concrete detail added by this specification set to make a
  requirement implementable. It is not itself a newly confirmed user requirement;
  implementation may refine it without changing the underlying requirement.
- **Deferred** — explicitly not a v1.0 requirement; **Open** — not yet decided
  (no decision remains open as of 2026-11-20).

## 6. Cross-cutting invariants

**REQ-PROJ-04 (Required).** Platform-dependent capabilities must be reported as
`unsupported` or `unavailable` rather than silently omitted. *Source: BR §17.*

**REQ-PROJ-05 (Required).** Workload and benchmark configuration, signal-generation
parameters, and relevant settings must be snapshotted into results. A configuration
reused on another machine must identify unsupported capabilities and environmental
differences rather than silently changing the test. *Source: BR §4.3, §13.*

**REQ-PROJ-06 (Required).** Compared builds receive identical input data wherever
possible. *Source: BR §4.3.*

**REQ-PROJ-07 (Required).** Completed measurements are preserved; failures and their
context are recorded; incomplete runs are marked incomplete and never presented as
successful complete benchmarks; result files are not corrupted on failure.
*Source: BR §14.*

**REQ-PROJ-08 (Required).** Input generation and setup stay outside the primary DSP
measurement interval unless end-to-end timing is explicitly requested.
*Source: BR §4.2, §7.4.*

**REQ-PROJ-11 (Confirmed).** The multi-file documentation layout in §4 is adopted;
`DECISIONS.md` remains the baseline record. *Source: BR §18 Q48, §21.*

## 7. Decision register

All three questions raised in BR §18 are resolved. Q46 and Q48 are confirmed
requirements; Q47 is an explicit deferral. No decision remains open.

### Q46 — Core language standard (confirmed 2026-11-20)

| | |
| --- | --- |
| Decision | C++20 for core, CLI and SDK convenience wrappers; versioned C ABI at the integration boundary; CMake for the build system; TUI in Rust with ratatui (amendment 2026-11-22) |
| Effect | Fixes the Foundation phase toolchain and the SDK wrapper language; recorded as REQ-PROJ-09 |
| Status | **Confirmed**; TUI clause superseded 2026-11-22 by the BR §21 TUI-technology confirmation |

### Q47 — Benchmark suites (deferred 2026-11-20)

| | |
| --- | --- |
| Considered feature | User-defined suites containing multiple processors and configurations, runnable as one operation, e.g. `ztick-bench suite run bass-dsp-suite.json` |
| Decision | **Deferred to post-v1.0.** No `suite` CLI command, suite schema or acceptance criterion is added |
| Design constraint | The portable config format remains compatible with a future suite layer ([RESULTS §6](RESULTS_FORMAT.md#6-config-document)) |
| Status | **Deferred — not a v1.0 requirement** |

### Q48 — Documentation structure (confirmed 2026-11-20)

| | |
| --- | --- |
| Decision | Adopt the multi-file layout used by this repository: `AGENTS.md`, `README.md` and the topic-owned files under `docs/` |
| Effect | The ownership map in §4 and `AGENTS.md` govern where detail lives; cross-topic content is linked, not copied |
| Status | **Confirmed** |

## 8. Traceability

`BR §n` means section *n* of the [Baseline Record](DECISIONS.md), which remains
authoritative for decisions. Each detailed document cites it for every normative
statement it restates, and marks concrete design detail as "Derived design".
Acceptance verification is mapped to BR §17 in
[TESTING_STRATEGY](TESTING_STRATEGY.md#10-acceptance-criteria-traceability).
