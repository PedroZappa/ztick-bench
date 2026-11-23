# ztick-bench

**General-purpose, offline-first DSP and audio benchmarking application.**

> **Repository status:** requirements baseline and v1.0 technical specification complete;
> roadmap phases 1–2 (Foundation, Benchmark engine) implemented and verified. Phase 3
> (Integrations) is in progress: the SDK runtime is compiled and link-tested and a
> minimal static adapter registers and is discoverable through the private host registry,
> while full adapters, trust and runners remain to be built — see [ROADMAP](docs/ROADMAP.md).
> **v1.0 scope:** roadmap phases 1–6; phase 7 (macOS/Windows) is post-v1.0.

---

## What ztick-bench is

A standalone application for measuring the performance of DSP libraries and audio
applications. It is independent of ALIENMETAL and defines its own development
standards in [AGENTS.md](AGENTS.md). It is licensed under the
[MIT License](LICENSE).

- **General-purpose core.** The core is not coupled to any one DSP library, audio
  framework, product, or application.
- **Offline first.** Offline DSP benchmarking is the first implementation priority;
  simulated real-time and live-audio benchmarking are roadmap items.
- **Linux first**, then macOS, then Windows.
- **Two measurement worlds, never conflated.** Black-box process monitoring and
  instrumented DSP/callback measurements are both supported, but results must always
  distinguish between them.

See [PROJECT_SPEC](docs/PROJECT_SPEC.md) for scope and [GLOSSARY](docs/GLOSSARY.md)
for canonical terminology.

## What ztick-bench is not (yet)

| Not in v1.0 | Where it is recorded |
| --- | --- |
| Live-audio benchmarking | [ROADMAP](docs/ROADMAP.md) |
| Simulated real-time benchmarking | [ROADMAP](docs/ROADMAP.md) |
| Integrations for REAPER, Ardour, Bitwig Studio, Pure Data, SuperCollider | [ROADMAP](docs/ROADMAP.md) |
| SQLite historical storage, plugin marketplace, extra input generators | [ROADMAP](docs/ROADMAP.md) |
| Benchmark suites (`suite run`) | Deferred to post-v1.0 (Q47) — [ROADMAP](docs/ROADMAP.md) |

## Documentation map

The [decision record](docs/DECISIONS.md) is the source of truth for **what** was
decided. The documents below own the **technical detail**; each links back to its
record sections.

| Document | Authoritative for |
| --- | --- |
| [PROJECT_SPEC.md](docs/PROJECT_SPEC.md) | Scope, v1.0 boundary, document ownership, decision register |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Components, dependency direction, build targets, host plugin/trust runtime |
| [SDK_SPECIFICATION.md](docs/SDK_SPECIFICATION.md) | C integration ABI, C++ wrappers, adapter contract, versions/capabilities |
| [BENCHMARK_SPECIFICATION.md](docs/BENCHMARK_SPECIFICATION.md) | Workloads, generators, profiles, execution lifecycle, validation behavior |
| [METRICS_REFERENCE.md](docs/METRICS_REFERENCE.md) | Metric definitions, formulas, statuses, aggregation, regression statistics |
| [TUI_SPECIFICATION.md](docs/TUI_SPECIFICATION.md) | Dashboard and screen-level TUI behavior |
| [PLATFORM_MONITORING.md](docs/PLATFORM_MONITORING.md) | System info, thermal/frequency monitoring, affinity, scheduling, priority |
| [RESULTS_FORMAT.md](docs/RESULTS_FORMAT.md) | JSON/CSV schemas, config format, metadata, portability, failure encoding |
| [TESTING_STRATEGY.md](docs/TESTING_STRATEGY.md) | Test layers, custom SDK apps, acceptance-criteria traceability |
| [ROADMAP.md](docs/ROADMAP.md) | Phase ordering, deferred work, decision dependencies |
| [GLOSSARY.md](docs/GLOSSARY.md) | Canonical definitions for all domain terms |
| [AGENTS.md](AGENTS.md) | Concise rules for coding agents and contributors |

## Build and test commands

C++20 and CMake are confirmed for the core, CLI and SDK (Q46); the integration
boundary is a versioned C ABI with C++ wrappers. The TUI is the Rust/ratatui
exception confirmed in BR §21. The toolchain targets GCC 13+/Clang 16+, uses
Catch2 v3 as a dev-only test framework, and is at tool version `0.2.0-dev` with
SDK ABI `1.0.0` (BR §21–§22).
The Phase 5 TUI is an optional Rust/ratatui front end (0.30.2, MIT; Rust 1.88+)
that calls the C ABI via FFI. Rust and Cargo are needed only when the TUI target
is enabled; core and CLI builds stay C++20-only. The source layout is in
[ARCHITECTURE §3.1](docs/ARCHITECTURE.md#31-source-tree-layout-derived-design).

```bash
make build               # wraps: cmake -S . -B build && cmake --build build
make test                # wraps: ctest --test-dir build --output-on-failure
make sysinfo             # print the inventory with availability states
```

Run the full CI gate locally — warnings-as-errors build, tests, formatting,
static analysis, Doxygen, sanitizers and the SDK consumer check:

```bash
make verify              # CI_CMAKE_ARGS overrides the default -DZTICK_WERROR=ON
```

The verification gate is defined for Doxygen 1.9.8, Cppcheck 2.13 and
clang-format 18 (the Ubuntu 24.04 CI toolchain); C/C++ are built with
`-DZTICK_WERROR=ON`, and tests run under ASan/UBSan with `TERM` unset. These are
development-tool requirements only: no third-party runtime library is required
by the core or CLI (BR §15).

## Contributing to the specification

Read [AGENTS.md](AGENTS.md) and §19 of the [decision record](docs/DECISIONS.md).
In short: confirmed decisions are requirements, unresolved questions are not, future
ideas are recorded separately, and changing an agreed requirement needs explicit
approval.
