# Glossary

| | |
| --- | --- |
| **Status** | Normative terminology for all ztick-bench documents |
| **Authoritative for** | Canonical definitions of domain terms |
| **Source** | [DECISIONS.md](DECISIONS.md) and the detailed specification documents |

Terms are defined once here. Other documents use these definitions without
redefining them.

## Normative language

Labels used in requirement statements; see
[PROJECT_SPEC §5](PROJECT_SPEC.md#5-normative-language).

| Term | Definition |
| --- | --- |
| **Required / Must / Shall** | A confirmed requirement from the Baseline Record. |
| **Expected** | Baseline guidance written as "should": a strong expectation, not an absolute mandate. |
| **Derived design** | Concrete detail added by the specification set to make a requirement implementable; refinement does not change the underlying requirement. |
| **Deferred** | Explicitly not a v1.0 requirement. |
| **Open** | Not yet decided; no decision remains open as of 2026-11-20. |

## Technologies and toolchain

| Term | Definition |
| --- | --- |
| **ratatui** | The MIT-licensed Rust terminal-UI library (version 0.30.2 in v1.0 planning) used for the dashboard front end; see [TUI §1](TUI_SPECIFICATION.md#1-principles). |
| **Cargo** | Rust's package manager and build tool; invoked by CMake only for the optional TUI target. |
| **FFI** | Foreign-function interface: the Rust TUI's calls into the versioned `ztick_` C ABI; no C++ types or exceptions cross this boundary. |

## Project and targets

| Term | Definition |
| --- | --- |
| **ztick-bench** | The standalone DSP/audio benchmarking application specified by this document set. |
| **DSP processor** | A single processing unit (filter, effect, algorithm) that can be benchmarked independently. |
| **Processing chain** | A sequence of DSP processors benchmarked as one workload. |
| **DSP engine** | An integrated processing system benchmarked as a whole, potentially with internal multithreading. |
| **Integration** | A DSP library or application connected to ztick-bench through the SDK. May be static or dynamic. |
| **Adapter** | The integration-side implementation of the SDK adapter contract: capabilities, configuration, execution, measurements, validation, reporting. |
| **Plugin** | A dynamically loaded integration artifact, subject to discovery, trust and isolation rules. |
| **Custom test application** | An independent application built against the SDK to validate the integration model; at least two are required for v1.0. |
| **Host harness** | The host-side executable that drives an adapter through the full SDK lifecycle and records observable evidence of each step. |
| **Out-of-tree build (external-style build)** | A build configured as an independent CMake project against the installed SDK package, using only installed headers and the exported `ztick::sdk` target, with no modification of or build dependency on the ztick-bench source tree (BR §17.8). |
| **Benchmark suite (deferred)** | A user-defined collection of multiple processors and configurations runnable as one operation; post-v1.0 per Q47. |

## Modes and measurement levels

| Term | Definition |
| --- | --- |
| **Offline benchmark** | Processing of pre-generated audio blocks without a live audio device. First implementation priority. |
| **Simulated real-time benchmark** | Offline processing driven against a synthetic deadline model. Roadmap item. |
| **Live audio benchmark** | Benchmarking against a real audio device and its callbacks. Roadmap item. |
| **Black-box monitoring** | Process-level observation of an unmodified application (CPU, memory, threads, system metrics). |
| **Instrumented monitoring** | Measurement obtained through SDK hooks that the application calls to expose DSP/callback timings. |
| **Workload** | The combination of processors, input signal and execution settings that a benchmark run applies. |
| **Input generator** | A deterministic producer of audio data for a workload (v1.0: sine, multi-tone/harmonic, impulse). |
| **Profile** | A named, reusable set of benchmark settings defining more than duration: warm-up, repetitions, coverage, detail and reliability checks. |
| **Dimension** | A configurable benchmark axis: sample rate, block size, channels, oversampling, instances, duration, iterations, repetitions. |
| **Test point** | One combination of workload and dimensions whose lifecycle is sequenced as generation, setup, warm-up, primary DSP interval and validation (BENCHMARK §6). |
| **Iteration** | One measured execution within a single repetition or test point. |
| **Repetition** | A repeated measurement of the same test point, used to establish variability. |
| **Warm-up** | Unmeasured execution before the measured interval, used to stabilize caches and frequency. |
| **Run** | One execution of a workload under a specific configuration. |
| **Session** | A collection of related runs, comparisons and metadata that can be exported and imported. |

## Timing and metrics

| Term | Definition |
| --- | --- |
| **Per-block timing** | Duration of an individual audio-block processing call. |
| **Whole-run timing** | Total duration of the benchmark run. |
| **End-to-end timing** | Optional timing that includes input generation, data movement, setup or other workload overhead. |
| **DSP interval** | The primary timed section, isolated to DSP processing; generation and setup are outside it unless end-to-end timing is selected. |
| **Measured value** | A directly observed metric value. |
| **Estimated value** | A derived metric value, including overhead estimates and black-box approximations. |
| **Unavailable value** | A metric the platform or integration could not provide. |
| **Not-applicable value** | A metric that is undefined for the benchmark type or mode. |
| **Overhead estimate** | A separately preserved estimate of the cost introduced by measurement and instrumentation; never silently subtracted. |
| **Raw basis** | The single retained population from which aggregates are computed: per-block durations of one run or whole-run durations across repetitions. Populations are never mixed. |
| **Nearest-rank quantile** | A percentile taken as the observed sample at ascending rank `ceil(q × n)`, with no interpolation; used for P90, P95, P99 and P99.9. |
| **Throughput** | Processing rate in frames/s (canonical); samples/s = frames/s × channels and blocks/s = frames/s ÷ block size. Samples are counted per channel. |
| **Real-time factor** | Ratio of DSP processing time to the duration of the audio represented; below 1 means faster than real time. |
| **Scaling efficiency** | How processing capacity grows with added instances or worker threads relative to ideal linear scaling. |
| **Deadline miss** | Failure to complete processing within a configured deadline; meaningful for simulated real-time or live modes, otherwise not applicable. |
| **Regression** | A performance difference that is both beyond the configured threshold and statistically significant, after comparability checks. |

## Audio data

| Term | Definition |
| --- | --- |
| **Sample rate** | Samples per second, explicitly declared for a workload (v1.0: 44.1–192 kHz values). |
| **Frame index** | Zero-based position `n` of a sample in a generated signal; the independent variable of the generator formulas. |
| **Tone weight** | Normalised coefficient `a_k` of a multi-tone generator; absolute weights sum to one so `|x[n]| <= A`. |
| **Block size** | Number of samples processed per call (v1.0: 16–2048). |
| **Channel layout** | Arrangement of channel samples: interleaved or planar. |
| **Interleaved** | Layout where samples for all channels are alternated within one buffer. |
| **Planar** | Layout where each channel has its own contiguous buffer. |
| **float32 / float64** | IEEE-754 single/double precision sample formats; float32 mandatory, float64 optional. |
| **Oversampling** | Processing at a multiple of the workload sample rate (v1.0: 1x–16x). |
| **Instance** | An independent concurrent copy of a DSP processor or engine. |

## SDK and plugins

| Term | Definition |
| --- | --- |
| **SDK** | The ztick-bench integration software development kit: versioned C ABI, C++ convenience wrappers and adapter contract. |
| **C ABI** | The stable, C-compatible binary interface at the integration boundary, avoiding dependence on a stable C++ ABI. |
| **Capability** | A declared support statement from an adapter, e.g. a sample format, channel layout or benchmark feature. |
| **Typed parameter** | An adapter-declared configuration value with a type, valid range, default and description. |
| **SDK version check** | Mandatory negotiation of SDK version and capabilities before an integration is used. |
| **Capability negotiation** | Host-side check that required capability bits are known and declared by an adapter before use; unknown bits are refused as unsupported (SDK_SPECIFICATION §4). |
| **Struct layout version** | The value required in a public struct's `version` header field, published as `ZTICK_STRUCT_LAYOUT_VERSION`; independent of the SDK ABI version (SDK_SPECIFICATION §1.1). |
| **Adapter registry** | The host-owned, thread-safe set of non-owning static adapter descriptor pointers populated by `ztick_register_adapter` (SDK_SPECIFICATION §2). |
| **Metadata-only inspection** | Reading a dynamic adapter's exported read-only descriptor as a data symbol without calling any plugin code; yields an owning metadata copy and no registration or trust (SDK_SPECIFICATION §10). |
| **Trusted plugin** | A plugin explicitly installed or supplied by the user and recorded in the trusted-plugin list. |
| **Trusted-plugin list** | The persisted set of approved plugin identifiers; an identifier absent from it triggers first-run confirmation. |
| **Trust authorization** | The host-side decision that permits execution only when a plugin is already trusted or the user explicitly approves first use; denial, absent consent and storage failures refuse with a reason. |
| **First-run confirmation** | Explicit user consent required before an untrusted plugin is executed for the first time. |
| **Runner selection** | The metadata-only decision that chooses in-process or out-of-process execution from the requested mode, prior trust authorization and backend availability; the chosen mode is recorded in the run record. |
| **In-process execution** | Running an integration inside the benchmark process for simplicity and low overhead. |
| **Out-of-process execution** | Running an integration in a separate process for fault isolation; not a complete security sandbox. |
| **Child process launcher** | Platform-specific host abstraction that creates, reaps and reads the final status of a child workload without loading plugin code into the host. |
| **Final-status IPC** | Fixed one-message transport that reports only the child workload's final status after its timed work; it carries no per-block samples. |

## Platform and reliability

| Term | Definition |
| --- | --- |
| **Platform abstraction** | The interface that isolates OS-specific system information, monitoring and scheduling from the portable core. |
| **Convenience mode** | Reliability defaults intended for practical everyday use. |
| **Controlled/reproducibility mode** | Stronger controls and environment checks intended for repeatable comparisons. |
| **Thermal throttling** | Reduction of CPU frequency due to temperature; a warning condition, not an automatic test rejection in v1.0. |
| **CPU affinity** | Restriction of threads to a chosen set of CPUs. |
| **Physical core / logical core** | Hardware core versus SMT thread; distinguishable where the platform exposes it. |
| **Scheduling priority** | OS thread/process priority; unchanged by default, optionally raised only with explicit opt-in and warnings. |

## Results and validation

| Term | Definition |
| --- | --- |
| **Result document** | The canonical versioned JSON representation of a run, including metadata and per-metric statuses. |
| **Config document** | The portable, versioned JSON description of a benchmark configuration. |
| **Config-shaped data** | The in-memory object/array/scalar representation of a config document used by core models before JSON serialization; owned by the core `config_value` model, with the portable document shape specified in [RESULTS §6](RESULTS_FORMAT.md#6-config-document). |
| **Build metadata** | Recorded git commit, compiler, version, optimization flags, build configuration and toolchain details. |
| **Machine metadata** | Recorded CPU, memory, cache, SIMD, OS, kernel and environment information. |
| **Validation level** | Basic (NaN/Inf, invalid output), Standard (+ reference comparisons), or Strict (+ adapter checks and broader tests); independently selectable. |
| **Check outcome** | The recorded result of one validation check, including its status, tolerances, reference source and observed error; a single pass/fail flag is not sufficient. |
| **Reference output** | Expected output supplied by an adapter, a workload reference file, deterministic generation, or a designated reference implementation. |
| **Reference source** | The origin of a reference output: adapter data, workload file, deterministic generation, or a designated reference implementation. |
| **Tolerance** | Configurable absolute and relative error limits for reference comparisons. |
| **Workload failure** | An adapter crash, invalid data or processing failure. |
| **Infrastructure failure** | Plugin load failure, benchmark process error or unavailable system monitoring. |
| **Measurement warning** | A condition such as thermal throttling or unexpected CPU load that reduces confidence in results. |
| **Incomplete run** | A run that failed or was interrupted; must be marked incomplete and never presented as a successful complete benchmark. |

## Documentation and process

| Term | Definition |
| --- | --- |
| **Baseline Record (BR)** | [DECISIONS.md](DECISIONS.md), the authoritative record of confirmed and deferred decisions; `BR §n` cites section *n* of it. |
| **Dev-only dependency** | A dependency used only for building or testing, never linked into installed runtime targets. Catch2 v3 is the sole example in v1.0 (BR §22). |
