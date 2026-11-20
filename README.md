# ztick-bench

**General-purpose, offline-first DSP and audio benchmarking application.**

> **Repository status:** requirements baseline and v1.0 technical specification complete; implementation not started.
> This repository currently contains **specification documents only**. No buildable
> application, `CMakeLists.txt`, or source tree exists yet — see
> [ROADMAP](docs/ROADMAP.md) for implementation sequencing.
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

## Planned build and test commands

These are **planned**, not yet usable (the Foundation phase has not landed).
C++20 and CMake are confirmed (Q46); the integration boundary is a versioned C ABI
with C++ wrappers. Phase 1 targets GCC 13+/Clang 16+, uses Catch2 v3 as a dev-only
test framework, and starts at tool version `0.1.0-dev` with SDK ABI `1.0.0` (BR §22).
The source layout is in
[ARCHITECTURE §3.1](docs/ARCHITECTURE.md#31-source-tree-layout-derived-design).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Contributing to the specification

Read [AGENTS.md](AGENTS.md) and §19 of the [decision record](docs/DECISIONS.md).
In short: confirmed decisions are requirements, unresolved questions are not, future
ideas are recorded separately, and changing an agreed requirement needs explicit
approval.
