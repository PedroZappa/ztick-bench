# ztick-bench

**General-purpose, offline-first DSP and audio benchmarking application.**

> **Repository status:** requirements baseline and v1.0 technical specification complete;
> roadmap phases 1–3 (Foundation, Benchmark engine, Integrations) implemented and
> verified. The SDK runtime is compiled and link-tested; metadata inspection,
> version/capability negotiation, trust enforcement, in-process and out-of-process
> runners, and both custom SDK apps are implemented. Static and dynamic integrations
> are also built and run out of tree against the installed SDK package, verifying
> [BR §17.8](docs/DECISIONS.md#17-v10-acceptance-criteria). Phase 4 (CLI and results) is
> in progress; the portable config and result schemas, JSON parser, atomic
> writer, CSV projection, availability-aware metadata attachment with
> comparability warnings, and the CLI dispatch skeleton with `list` are
> implemented — see [ROADMAP](docs/ROADMAP.md).
> **v1.0 scope:** roadmap phases 1–6; phase 7 (macOS/Windows) is post-v1.0.

---

## What ztick-bench is

A standalone application for measuring the performance of DSP libraries and audio
applications. It defines its own development standards in [AGENTS.md](AGENTS.md)
and is licensed under the [MIT License](LICENSE).

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
Catch2 v3 as a dev-only test framework, and is at tool version `0.5.0-dev` with
SDK ABI `1.0.0` (BR §21–§22).
The Phase 5 TUI is an optional Rust/ratatui front end (0.30.2, MIT; Rust 1.88+)
that calls the C ABI via FFI. Rust and Cargo are needed only when the TUI target
is enabled; core and CLI builds stay C++20-only. The source layout is in
[ARCHITECTURE §3.1](docs/ARCHITECTURE.md#31-source-tree-layout-derived-design).

```bash
make build               # wraps: cmake -S . -B build && cmake --build build
make test                # wraps: ctest --test-dir build --output-on-failure
make sysinfo             # print the inventory with availability states
make sdk-consumer-check  # build an out-of-tree find_package(ztick-sdk) consumer
make external-apps-check # build and run both integrations out of tree (P3.9)
make tui-check           # registered Rust TUI gates through CTest
make tui-coverage        # cargo-llvm-cov LCOV/HTML under build/coverage/rust
make tui-deny            # cargo-deny policy for the TUI dependency graph
```

Run the full CI gate locally — warnings-as-errors build, C/C++ tests, registered
Rust gates (formatting, clippy, rustdoc, all-targets tests, plus coverage and
dependency-policy gates when their tools are installed), static analysis,
Doxygen, sanitizers, the SDK consumer check and the external-style app check:

```bash
make verify              # CI_CMAKE_ARGS overrides the default -DZTICK_WERROR=ON
```

The verification gate is defined for Doxygen 1.9.8, Cppcheck 2.13,
clang-format 18 and Rust 1.88.0 (the Ubuntu 24.04 CI toolchain); C/C++ are built
with `-DZTICK_WERROR=ON`, and tests run under ASan/UBSan with `TERM` unset.
`rust-toolchain.toml` pins the Rust version and the `rustfmt`, `clippy`,
`rust-analyzer`, `rust-src` and `llvm-tools-preview` components for local
editors and CI.
The optional Rust tools are cargo-llvm-cov 0.9.2 and cargo-deny 0.20.2; CI
installs both and sets `ZTICK_REQUIRE_RUST_TOOLS=1` so they cannot be skipped.
These are development-tool requirements only: no third-party runtime library is
required by the core or CLI (BR §15).

## Building an integration out of tree

Install the SDK from a normal build and use the exported package in your own
CMake project (SDK_SPECIFICATION §13):

```bash
cmake --install build --prefix /path/to/prefix
```

```cmake
find_package(ztick-sdk 1.0 REQUIRED)
target_link_libraries(my_adapter PRIVATE ztick::sdk)
```

A dynamic adapter exports `ztick_adapter_export` and must not link `ztick::sdk`.
`tests/sdk_external_apps` contains working out-of-tree examples of both the static
and dynamic forms.

## Contributing to the specification

Read [AGENTS.md](AGENTS.md) and §19 of the [decision record](docs/DECISIONS.md).
In short: confirmed decisions are requirements, unresolved questions are not, future
ideas are recorded separately, and changing an agreed requirement needs explicit
approval.
