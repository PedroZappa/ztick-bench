# ztick-bench --- Decisions and Requirements Record

**Status:** Requirements baseline complete; all decisions resolved\
**Last updated:** 2026-11-20\
**Purpose:** Consolidate the decisions made during the ztick-bench
specification process. This document records agreed choices, accepted
recommendations and resolved questions. It is the requirements record; the
complete technical specification is in `README.md` and `docs/`.

*Formerly `brainstorm.md`. Citations written as `BR §n` in the specification
documents refer to sections of this record (BR = Baseline Record).*

------------------------------------------------------------------------

## 1. Project identity and purpose

-   **Project name:** `ztick-bench`
-   **Project type:** Standalone application, independent of ALIENMETAL.
-   **Engineering approach:** Follow the strict development standards
    and procedures established for the ALIENMETAL project, adapted to
    this independent project.
-   **Primary purpose:** A general-purpose audio/DSP benchmarking
    application suitable for evaluating DSP libraries and audio
    applications, including future projects and third-party
    integrations.
-   **Generality:** The core must not be coupled to one DSP library,
    audio framework, product, or application.
-   **Initial focus:** Offline DSP benchmarking.
-   **Future modes:** Simulated real-time benchmarking and live-audio
    benchmarking are on the roadmap.
-   **Platforms:** Linux first, macOS second, Windows as a later roadmap
    target.
-   **Language/build direction (confirmed):** C++20 for the core engine,
    CLI, TUI and SDK convenience wrappers; versioned C-compatible ABI at
    the integration boundary; CMake for the build system. Confirmed under
    Question 46 on 2026-11-20.

## 2. Benchmark targets and operating modes

### 2.1 Target types

The application must support benchmarking:

1.  DSP libraries through direct integrations/adapters.
2.  Audio applications through process monitoring and, where available,
    application-specific instrumentation.

### 2.2 Application monitoring capabilities

Support two distinct levels of audio-application measurement:

-   **Black-box monitoring:** Observe an application without modifying
    it. Collect process-level information such as CPU use, memory use,
    thread count, and available system metrics.
-   **Instrumented monitoring:** Applications integrate with the
    ztick-bench SDK to expose more detailed information, including
    DSP/audio processing timings where the application can provide them.

The results and UI must clearly distinguish process-level measurements
from instrumented DSP/callback measurements. They must not be presented
as equivalent.

### 2.3 Benchmark modes

Roadmap:

1.  Offline DSP benchmarks --- first implementation priority.
2.  Simulated real-time benchmarks --- later implementation.
3.  Live audio benchmarks --- later implementation.

The architecture should accommodate all three without forcing live-audio
or platform-specific assumptions into the initial offline core.

## 3. Benchmark dimensions and profiles

### 3.1 Configurable dimensions

The benchmark configuration must support these dimensions:

  Dimension      Initial values / behavior
  -------------- -----------------------------------------------
  Sample rate    44.1, 48, 88.2, 96, 176.4, 192 kHz
  Block size     16, 32, 64, 128, 256, 512, 1024, 2048 samples
  Channels       1, 2, 4, 8, 16
  Oversampling   1x, 2x, 4x, 8x, 16x
  Instances      1, 2, 4, 8, 16, 32 and higher
  Duration       Configurable target duration
  Iterations     Configurable fixed iteration count
  Repetitions    Configurable repeated measurements

Not every possible combination must be executed. Profiles and custom
configurations determine test coverage.

### 3.2 Benchmark profiles

Provide the following built-in profiles:

  ------------------------------------------------------------------------
  Profile                   Approximate duration per Purpose
                                                test 
  --------------------- ---------------------------- ---------------------
  Quick                                5--10 seconds Fast development
                                                     feedback

  Standard                            20--30 seconds Balanced everyday
                                                     benchmarking

  Comprehensive                   60 seconds or more Broad coverage and
                                                     stronger statistical
                                                     confidence

  Custom                                User-defined User-selected test
                                                     coverage and settings
  ------------------------------------------------------------------------

Profiles must define more than duration. They should also specify
warm-up, repetitions, test coverage, measurement detail, and reliability
checks.

Users must be able to create and save their own named profiles.

Both target-duration and fixed-iteration execution modes must be
supported.

## 4. Workload model and input generation

### 4.1 Workload levels

The first release must support three workload levels:

1.  **Individual processors** --- benchmark a single DSP processor.
2.  **Processing chains** --- benchmark a sequence or chain of
    processors.
3.  **Complete DSP engines** --- benchmark an integrated DSP engine.

### 4.2 Initial built-in input generators

The first release must include:

-   Sine wave.
-   Multi-tone / harmonic signal.
-   Impulse.

Other generators are future extensions and should be noted in the
design, without being required for the first release:

-   White noise.
-   Pink noise.
-   Frequency sweeps.
-   Silence.
-   Musical test material.
-   Custom audio files.

Workload input generation and setup should occur outside the primary DSP
timing interval unless the selected test explicitly requests end-to-end
measurement.

### 4.3 Reproducibility

-   Randomized workloads in future extensions must support configurable
    seeds.
-   Signal-generation parameters must be fixed and recorded for a given
    run.
-   Workload and benchmark configuration must be snapshotted into
    results.
-   Compared builds should receive identical input data wherever
    possible.
-   Relevant benchmark settings must be recorded to make runs
    reproducible.

## 5. DSP integration SDK

### 5.1 Integration model

Provide both:

-   Statically linked integrations compiled into ztick-bench.
-   Dynamically loaded integrations provided as shared libraries.

Use a **versioned C-compatible integration interface**, with C++
convenience wrappers. The C-compatible boundary is intended to avoid
depending on a stable C++ ABI across different compilers and platforms.

The SDK must define a common adapter contract for capabilities,
configuration, workload execution, measurements, validation, and result
reporting. SDK version and capability checks must be performed.

### 5.2 Initial integration validation

Initially validate the SDK using custom applications. Integrations for
REAPER, Ardour, Bitwig Studio, Pure Data, SuperCollider, and other
established applications are future possibilities, not first-release
dependencies.

At least two independent custom test applications must validate the SDK
for v1.0.

### 5.3 Adapter-declared parameters and capabilities

Adapters must be able to declare:

-   Supported sample formats and channel layouts.
-   Supported benchmark capabilities.
-   Typed configuration parameters.
-   Parameter types, valid ranges, defaults, and descriptions.
-   Unsupported configurations, so the application can explain why a
    test cannot run.

Examples of adapter-specific parameters include oversampling factor,
filter mode, quality setting, internal processing mode, voice count, and
processing stages.

### 5.4 Audio data formats

Initial SDK audio data requirements:

-   `float32` is mandatory.
-   `float64` is optional.
-   Interleaved and planar channel layouts.
-   Explicit sample rate, channel count, and block size.
-   Integrations declare unsupported formats and capabilities.

## 6. Plugin discovery, trust, and isolation

### 6.1 Discovery

Support:

-   Explicit plugin paths supplied by the user or CLI.
-   A user plugin directory.
-   A project-local plugin directory.
-   Metadata inspection without executing the plugin.
-   No automatic execution merely because a plugin is discovered.

A plugin marketplace or online package manager is not required for v1.0.

### 6.2 Trust requirements

-   Plugins must be explicitly installed or supplied by the user.
-   Display plugin identity, version, SDK compatibility, and declared
    capabilities.
-   Do not automatically load or execute arbitrary plugins.
-   Maintain a trusted-plugin list.
-   Require explicit user confirmation the first time an untrusted
    plugin is executed.

### 6.3 Execution isolation

Support both execution modes:

-   **In-process:** For trusted integrations where simplicity and low
    overhead are appropriate.
-   **Out-of-process:** For untrusted integrations and cases requiring
    stronger fault isolation.

Record which execution mode was used. Process management, IPC, and
communication overhead must be kept outside the measured DSP execution
interval wherever practical.

Process isolation reduces the impact of crashes but must not be
represented as a complete security sandbox.

## 7. Benchmark metrics and measurement methodology

### 7.1 Initial metric set

All of the following metrics are included in the first release, subject
to applicability and availability for the benchmark type:

  Category              Metrics
  --------------------- ------------------------------------------------
  Timing                Execution time, mean, median, minimum, maximum
  Statistical           Standard deviation, P90, P95, P99, P99.9
  Performance           Throughput, real-time factor
  Resource usage        CPU utilization, memory usage
  Parallelism           Scaling efficiency
  Real-time behavior    Deadline misses
  Measurement quality   Benchmark overhead estimates

For each metric, results must distinguish between:

-   Measured value.
-   Estimated value.
-   Unavailable value.
-   Not-applicable value.

This is especially important when comparing black-box process monitoring
with instrumented DSP measurements.

### 7.2 Timing levels

Implement the following complementary timing levels:

1.  **Per-block timing:** Individual audio-block processing durations.
2.  **Whole-run timing:** Total benchmark execution duration.
3.  **End-to-end timing:** Optional timing including input generation,
    data movement, setup, or other workload overhead.

Parallel benchmarks should additionally collect wall-clock execution
time and worker-thread CPU time where available.

### 7.3 Default measurement detail

-   Use a lower-overhead aggregate measurement mode by default.
-   Offer detailed per-block timing as an option.
-   Allow detailed collection to be configured.
-   Estimate and report measurement overhead.
-   Preserve raw measurements and overhead estimates independently.
-   Do not silently subtract estimated overhead from reported
    measurements.
-   Investigate and test the overhead introduced by timing itself,
    particularly for small block sizes.

### 7.4 Timing boundaries

The primary DSP measurement must isolate DSP processing. Input
generation and setup should happen outside the timed section unless
end-to-end measurement is explicitly selected.

## 8. Parallelism and CPU configuration

Support both:

1.  Multiple independent DSP instances running concurrently.
2.  DSP engines that perform their own internal multithreaded
    processing.

Collect thread counts, CPU affinity, and scheduling configuration where
available.

Support:

-   Automatic/default scheduling.
-   User-selectable CPU affinity.
-   Physical-core versus logical-core selection where detectable.
-   Configurable worker-thread count.
-   Optional CPU isolation from other work where the operating system
    permits it.

### 8.1 Process scheduling priority

-   Respect the existing system scheduling priority by default.
-   Record the current priority in benchmark metadata.
-   Optionally allow the user to request higher priority for controlled
    benchmarking.
-   Require explicit user opt-in and show clear warnings about system
    responsiveness and possible measurement bias.
-   Check permissions and gracefully fall back if the OS refuses the
    request.
-   Do not unexpectedly alter system scheduling behavior.

## 9. System information and environmental reliability

Collect the proposed system information, where available:

-   CPU model and architecture.
-   Physical and logical core counts.
-   CPU frequency and scaling behavior.
-   Cache information where detectable.
-   SIMD capabilities.
-   RAM information.
-   OS and kernel information.
-   Audio backend and device information when relevant.
-   CPU affinity and scheduling configuration.
-   Other relevant system/environment metadata.

Monitor and record, where available:

-   CPU frequency before, during, and after a run.
-   CPU temperature.
-   Thermal throttling indicators.
-   Frequency scaling and power-management information.

For thermal, frequency, background-load, or other environmental
instability:

-   Warn the user.
-   Record the condition in the result.
-   Do not silently discard measurements or claim the run was fully
    controlled.
-   The first-release decision is to warn rather than automatically
    reject or repeat tests because of these conditions.

Provide two reliability approaches:

-   **Convenience mode:** Practical defaults for everyday use.
-   **Controlled/reproducibility mode:** Stronger controls and
    environment checks for repeatable comparisons.

Reliability features include warm-up, repeated measurements, outlier
detection, configurable duration, background CPU-load awareness,
thermal/frequency monitoring, environment metadata, and
measurement-overhead estimation.

## 10. Correctness validation

Correctness validation must be separate from timed performance
measurements.

### 10.1 Validation levels

Provide three independently selectable levels:

-   **Basic:** Detect NaN, infinity, invalid output, and obvious
    processing failures.
-   **Standard:** Basic validation plus reference-output comparisons
    where available.
-   **Strict:** Standard validation plus adapter-specific checks and
    more extensive input/output testing.

### 10.2 Output checks and reference sources

Support:

-   NaN/Inf detection.
-   Unexpected clipping or out-of-range values where applicable.
-   Reference-output comparisons with configurable tolerances.
-   Detection of whether output changes when expected.
-   Adapter-provided custom correctness checks.
-   Reference data supplied by adapters.
-   Reference files supplied with workloads.
-   Deterministic reference generation for simple built-in workloads.
-   Configurable absolute and relative error tolerances.
-   Direct comparison of two implementations of the same DSP algorithm,
    with one designated as the reference.

Validation must not distort the primary performance timing.

## 11. Build metadata and comparisons

### 11.1 Build metadata

Automatically record, where discoverable:

-   Git commit hash.
-   Compiler and compiler version.
-   Optimization flags.
-   Build configuration.
-   Relevant build/toolchain information.

### 11.2 Comparison levels

Support:

1.  **Run-to-run:** Repeated executions of the same build to establish
    variability.
2.  **Build-to-build:** Compare commits, compiler settings, optimization
    configurations, and builds.
3.  **Machine-to-machine:** Compare results across different CPUs and
    systems, with hardware/environment metadata and comparability
    warnings.

### 11.3 Regression detection

-   Support configurable percentage thresholds.
-   Calculate statistical significance before reporting a regression.
-   Avoid reporting small differences within normal measurement
    variability as meaningful regressions.
-   Preserve the underlying measurements and comparison context.
-   Warn when hardware or software environments are not sufficiently
    comparable.
-   Distinguish raw performance differences from genuine regressions.

## 12. User interfaces and output

### 12.1 TUI

Provide a modern, dashboard-first TUI, with traditional
menus/configuration/detail screens where useful.

### 12.2 CLI

Provide a full-fledged command-oriented CLI. Proposed initial command
structure:

``` bash
ztick-bench system
ztick-bench list
ztick-bench run
ztick-bench compare
ztick-bench report
ztick-bench plugin list
ztick-bench plugin inspect
```

The CLI must support:

-   Configuration files.
-   Command-line overrides.
-   CI-friendly exit codes.
-   Machine-readable output.

### 12.3 Results and storage

For the first release:

-   JSON is the canonical complete result format.
-   CSV is supported for convenient data analysis.
-   Benchmark sessions can be exported and imported.
-   Build, machine, workload, and environment metadata are attached to
    each run.
-   JSON benchmark configurations are portable and versioned.

SQLite is deferred to a later implementation for local historical
storage and querying. Do not make SQLite a first-release requirement.

## 13. Portable benchmark configurations

Store configurations as versioned JSON. Include:

-   SDK and configuration schema versions.
-   Integration identifier and version.
-   Workload and signal parameters.
-   Sample rate, block size, channels, instances, and oversampling.
-   Measurement and validation settings.
-   Profile and duration settings.
-   Relevant execution settings.

A configuration reused on another machine must identify unsupported
capabilities and environment differences rather than silently changing
the test.

## 14. Failure handling and result integrity

Distinguish:

-   **Workload failure:** The DSP adapter crashes, returns invalid data,
    or fails to process.
-   **Infrastructure failure:** A plugin cannot load, the benchmark
    process encounters an error, or system monitoring becomes
    unavailable.
-   **Measurement warning:** Thermal throttling, unexpected CPU load, or
    another condition makes results less reliable.

Required behavior:

-   Preserve all completed measurements.
-   Record failures and their context.
-   Continue with independent tests where safe.
-   Prevent a crashing plugin from bringing down the main TUI or CLI
    where process isolation is used.
-   Clearly mark incomplete runs as incomplete.
-   Never present an incomplete run as a successful complete benchmark.
-   Avoid corrupting result files when an integration or infrastructure
    component fails.

## 15. Dependencies and architecture principles

-   Use CMake.
-   Keep the core portable and independent of individual DSP
    libraries/frameworks.
-   Separate dependencies by responsibility: core engine/SDK, TUI, CLI,
    system monitoring, plugin loading, JSON serialization, and testing.
-   Prefer maintained, permissively licensed libraries.
-   Document dependency versions and licenses.
-   Optional features must not force unnecessary dependencies on users
    who only need the core engine or CLI.
-   Keep Linux-specific behavior behind platform abstractions so macOS
    and future Windows support remain feasible.

## 16. Development roadmap

The accepted roadmap is:

1.  **Foundation:** Repository, CMake, SDK, core types, system
    information.
2.  **Benchmark engine:** Workloads, execution, timing, statistics,
    validation.
3.  **Integrations:** Static and dynamic adapters, custom SDK test
    applications.
4.  **CLI and results:** Profiles, JSON, comparison, regression
    detection, CI.
5.  **TUI:** Dashboard, configuration, execution monitoring, result
    inspection.
6.  **Reliability:** Stress tests, isolation, overhead validation,
    repeatability.
7.  **Platform expansion:** macOS support and subsequent Windows
    preparation.

Validate the SDK and core engine through working custom applications
before investing heavily in the TUI.

## 17. v1.0 acceptance criteria

Version 1.0 is considered ready when:

1.  Linux is fully supported.
2.  At least two independent custom test applications validate the SDK
    integration model.
3.  Static and dynamic integrations work.
4.  All initial metrics and workload levels are implemented.
5.  CLI, JSON results, comparisons, and regression detection work.
6.  The TUI supports the principal workflows.
7.  Automated tests cover correctness, measurement overhead,
    repeatability, failure handling, and plugin isolation.
8.  Documentation is sufficient for a third-party developer to build an
    integration without modifying ztick-bench itself.

Platform-dependent capabilities must be explicitly reported as
unsupported or unavailable rather than silently omitted.

------------------------------------------------------------------------

## 18. Questions from the baseline process (resolved)

The following questions were raised after the baseline decisions. All three
have now been answered; their outcomes are recorded below and in §21. Earlier
drafts treated them as unresolved, but no question remains open.

### 46. Core language standard

Recommendation presented: C++20 for the core engine, CLI, TUI, and SDK
convenience wrappers; C-compatible ABI for the integration boundary;
CMake for the build system.

**Status:** Confirmed 2026-11-20. C++20 for core, CLI, TUI and SDK
convenience wrappers; versioned C-compatible ABI at the integration
boundary; CMake for the build system.

### 47. Benchmark suites

Recommendation presented: Support user-defined benchmark suites
containing multiple processors and configurations, runnable as a single
operation (for example, `ztick-bench suite run bass-dsp-suite.json`).

**Status:** Deferred 2026-11-20. Benchmark suites are post-v1.0 work. The
portable config format should remain compatible with a future suite layer, but
no `suite` CLI command, schema or acceptance criterion is a v1.0 requirement.

### 48. Documentation structure

Proposed structure:

``` text
ztick-bench/
├── AGENTS.md
├── README.md
├── CMakeLists.txt
└── docs/
    ├── PROJECT_SPEC.md
    ├── ARCHITECTURE.md
    ├── SDK_SPECIFICATION.md
    ├── BENCHMARK_SPECIFICATION.md
    ├── METRICS_REFERENCE.md
    ├── TUI_SPECIFICATION.md
    ├── PLATFORM_MONITORING.md
    ├── RESULTS_FORMAT.md
    ├── TESTING_STRATEGY.md
    ├── ROADMAP.md
    └── GLOSSARY.md
```

`AGENTS.md` should contain concise development rules, constraints,
commands, and coding-agent instructions---not the full project
specification.

**Status:** Confirmed 2026-11-20. The proposed multi-file layout is adopted,
with this file as the decisions ledger and one authoritative document per
topic under `docs/`.

------------------------------------------------------------------------

## 19. Specification maintenance rules

-   Treat decisions explicitly confirmed by the user as requirements.
-   Keep recommendations that the user accepted as requirements.
-   Do not silently convert unresolved questions into decisions.
-   Keep this consolidated record consistent with the detailed
    specification documents as they are created.
-   Avoid duplicating extensive technical detail across documents;
    define authoritative ownership for each topic and link to it from
    other documents.
-   Record future ideas separately from first-release requirements.
-   When implementation decisions require changing an agreed
    requirement, document the reason and obtain explicit approval before
    proceeding.

## 20. Specification documents

This record remains the authoritative decisions ledger. Detailed technical
specification is owned by the documents below, which cite `BR §n` for every
requirement they restate and mark added implementation detail as derived design.
The layout is the proposal in Question 48, **confirmed 2026-11-20**.

| Document | Authoritative for |
| --- | --- |
| `README.md` | Project introduction, documentation links |
| `AGENTS.md` | Concise development rules, commands, ownership map |
| `docs/PROJECT_SPEC.md` | Scope, v1.0 boundary, document ownership, decision register |
| `docs/ARCHITECTURE.md` | Components, dependency direction, build targets, host plugin/trust runtime |
| `docs/SDK_SPECIFICATION.md` | C integration ABI, C++ wrappers, adapter contract, version/capability checks |
| `docs/BENCHMARK_SPECIFICATION.md` | Workloads, generators, profiles, execution lifecycle, validation behavior |
| `docs/METRICS_REFERENCE.md` | Metric definitions, formulas, statuses, aggregation, regression statistics |
| `docs/TUI_SPECIFICATION.md` | Dashboard and screen-level TUI behavior |
| `docs/PLATFORM_MONITORING.md` | System info, monitoring, affinity, scheduling, priority |
| `docs/RESULTS_FORMAT.md` | JSON/CSV schemas, config format, metadata, portability, failure encoding |
| `docs/TESTING_STRATEGY.md` | Test layers, custom SDK apps, acceptance-criteria traceability |
| `docs/ROADMAP.md` | Phase ordering, deferred work, decision dependencies |
| `docs/GLOSSARY.md` | Canonical definitions for all domain terms |

Questions 46–48 are all resolved and are recorded in `docs/PROJECT_SPEC.md` §7
and in §21 below. No decision remains open.

## 21. Post-baseline confirmations (2026-11-20)

The following clarifications were confirmed after the baseline record was
finalised. Per §19, they are requirements or explicit scope decisions.

-   **Q46 (confirmed):** C++20 for the core engine, CLI, TUI and SDK
    convenience wrappers; versioned C-compatible ABI at the integration
    boundary; CMake for the build system.
-   **Q47 (deferred):** Benchmark suites are post-v1.0. The portable config
    format should remain compatible with a future suite layer; no `suite` CLI
    command, schema or acceptance criterion is added to v1.0.
-   **Q48 (confirmed):** The multi-file documentation layout in §20 is adopted.
-   **v1.0 boundary:** Roadmap phases 1–6 constitute v1.0. Phase 7 (macOS
    support and Windows preparation) is post-v1.0.
-   **Roadmap detail:** Phase deliverables and exit criteria are planning
    guidance, not binding gates. Only the BR §16 phase order and BR §17
    acceptance criteria are binding.
-   **SDK validation apps:** The two required custom test applications remain
    generic placeholders; no specific application or DSP library is mandated.
-   **Roadmap time:** Phases are dependency-ordered only; no dates or estimates
    are added.
-   **Dependencies:** No specific third-party library is mandated for JSON,
    TUI, testing or process IPC. BR §15 principles remain the constraint.

## 22. Pre-implementation decisions (2026-11-20)

Decisions made after the specification was completed, to unblock Phase 1.

-   **Project identity / standards:** ztick-bench is not ALIENMETAL and does not
    inherit its development standards. The BR §1 clause referencing those
    standards is superseded by concrete naming, formatting, commit and branch
    rules in `AGENTS.md`. Reason: the user clarified the project's identity.
-   **License:** MIT, with `SPDX-License-Identifier: MIT` on source files.
-   **Versioning:** tool `0.1.0-dev` during development; SDK ABI version `1.0.0`
    from Phase 1.
-   **Phase 1 system info:** a small developer `ztick-sysinfo` executable reports
    system information with availability states; the CLI `system` command remains
    in Phase 4.
-   **Toolchain:** GCC 13+ or Clang 16+; `-Wall -Wextra -Wpedantic`, `-Werror` in
    CI, ASan/UBSan in debug builds.
-   **Repository layout:** the revised layout is adopted — top-level `include/ztick/`
    as the installed include root, `tools/ztick-sysinfo/`, `tests/fixtures/`,
    `cmake/ztick-sdk-config.cmake.in` and `.github/workflows/`. Owned by
    [ARCHITECTURE §3.1](ARCHITECTURE.md#31-source-tree-layout-derived-design).
-   **SDK header contract:** frozen before the first public header is written —
    `<ztick/>` include root, `ztick_` symbol prefix, `ZTICK_` macro prefix, opaque
    handles, C11/fixed-width-only public surface and the canonical core-type
    inventory. Owned by
    [SDK_SPECIFICATION §1.1–§1.2](SDK_SPECIFICATION.md#11-frozen-public-header-contract-br-22).
-   **Test framework:** Catch2 v3, pinned as a dev-only dependency with a
    FetchContent fallback; never linked into installed targets. This is the single
    mandated test dependency; the §21 "no mandated third-party library" clause
    governs runtime/JSON/TUI/IPC choices. Owned by
    [TESTING_STRATEGY §1](TESTING_STRATEGY.md#1-testing-requirements).
-   **Development rules:** the naming, formatting, commit and branch rules in
    `AGENTS.md` are accepted as written; the copyright holder is "ztick-bench
    contributors".
