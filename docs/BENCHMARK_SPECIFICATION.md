# Benchmark Specification

| | |
| --- | --- |
| **Status** | Technical specification — v1.0 baseline complete |
| **Source of truth** | [DECISIONS.md](DECISIONS.md) BR §2–§4, §7.2–§7.4, §8, §10 |
| **Authoritative for** | Workloads, generators, dimensions/profiles, run lifecycle, parallelism behavior, validation behavior |
| **Not authoritative for** | Metric formulas/statuses ([METRICS](METRICS_REFERENCE.md)), platform monitoring details ([PLATFORM](PLATFORM_MONITORING.md)), serialization ([RESULTS](RESULTS_FORMAT.md)) |

---

## 1. Targets and measurement levels

The benchmark targets DSP libraries through adapters and audio applications through
black-box process monitoring plus optional instrumentation. Results and UI must
distinguish process-level from instrumented DSP/callback measurements and must not
present them as equivalent. *Source: BR §2.1–§2.2.*

Benchmark modes are offline (v1.0), then simulated real-time and live audio
(roadmap). The architecture must not force live-audio assumptions into the offline
core. *Source: BR §2.3; see [ROADMAP](ROADMAP.md).*

## 2. Workload levels

**REQ-BENCH-01 (Required).** The first release supports three workload levels:
individual processors, processing chains and complete DSP engines. *Source: BR §4.1.*

| Level | Meaning | Scope note |
| --- | --- | --- |
| Processor | One DSP processor benchmarked alone | Baseline unit of comparison |
| Chain | An ordered sequence of processors | Chain composition is part of the workload definition |
| Engine | An integrated DSP engine | May use internal multithreading (BR §8) |

## 3. Built-in input generators

**REQ-BENCH-02 (Required).** The first release includes sine wave, multi-tone /
harmonic signal and impulse generators. *Source: BR §4.2.*

**REQ-BENCH-03 (Required).** Signal-generation parameters are fixed and recorded
for a run; compared builds receive identical input data wherever possible.
*Source: BR §4.3.*

Derived design (the independent reference for generator tests): with frame index
`n` in `[0, frames)`, sample rate `fs`, peak amplitude `A` and fixed initial phase:

- **Sine:** `x[n] = A * sin(2*pi*f*n/fs + phi)`, with `f` and `phi` recorded.
- **Multi-tone / harmonic:** `x[n] = A * sum_k a_k * sin(2*pi*f_k*n/fs + phi_k)`,
  with tone frequencies, phases and weights recorded. Weights are normalised so
  `sum_k |a_k| = 1`, which bounds `|x[n]| <= A`.
- **Impulse:** `x[p] = A` at one recorded position `p`; all other samples are zero.

The v1.0 generators are deterministic; no random seed applies. Seeded generators
are deferred with the randomized generators above.

Deferred generators, recorded here so they are not mistaken for v1.0 requirements:
white noise, pink noise, frequency sweeps, silence, musical test material and
custom audio files. Randomized future generators must support configurable seeds
(BR §4.2–§4.3).

## 4. Benchmark dimensions

**REQ-BENCH-04 (Required).** Configurations support the following dimensions; not
every combination must be executed — profiles and custom configurations determine
coverage. *Source: BR §3.1.*

| Dimension | Initial values / behavior |
| --- | --- |
| Sample rate | 44.1, 48, 88.2, 96, 176.4, 192 kHz |
| Block size | 16, 32, 64, 128, 256, 512, 1024, 2048 samples |
| Channels | 1, 2, 4, 8, 16 |
| Oversampling | 1x, 2x, 4x, 8x, 16x |
| Instances | 1, 2, 4, 8, 16, 32 and higher |
| Duration | Configurable target duration |
| Iterations | Configurable fixed iteration count |
| Repetitions | Configurable repeated measurements |

Derived design: in duration mode, `frames = ceil(duration * fs)` and
`blocks = ceil(frames / block_size)` per instance; in iteration mode,
`frames = iterations * block_size`. If both duration and iterations are configured,
iterations take precedence and the conflict is recorded as a warning. Warm-up
iterations are excluded from frames, throughput and RTF denominators.

## 5. Profiles

**REQ-BENCH-05 (Required).** Built-in profiles are Quick, Standard, Comprehensive
and Custom; profiles define more than duration, including warm-up, repetitions,
test coverage, measurement detail and reliability checks; users can create and
save named profiles; both target-duration and fixed-iteration execution are
supported. *Source: BR §3.2.*

| Profile | Approximate duration per test | Purpose |
| --- | --- | --- |
| Quick | 5–10 seconds | Fast development feedback |
| Standard | 20–30 seconds | Balanced everyday benchmarking |
| Comprehensive | 60 seconds or more | Broad coverage and stronger statistical confidence |
| Custom | User-defined | User-selected coverage and settings |

A profile is a named set of dimension coverage plus measurement, reliability and
validation settings; its serialized form is owned by
[RESULTS_FORMAT](RESULTS_FORMAT.md#6-config-document).

## 6. Execution lifecycle and timing boundaries

**REQ-BENCH-06 (Required).** The primary DSP measurement isolates DSP processing;
input generation and setup occur outside the timed section unless end-to-end
measurement is explicitly selected. *Source: BR §4.2, §7.4.*

Derived design of one test point:

```text
 configure + capability check
        |
 generate input / allocate buffers / attach references   <-- outside timing
        |
 warm-up (profile-dependent)                             <-- outside timing
        |
 [ start primary DSP interval ]
        |  process blocks (aggregate or per-block timing)
 [ stop primary DSP interval ]
        |
 validation phase (separate from timing)
        |
 metadata attachment + result write
```

**REQ-BENCH-07 (Required).** Three complementary timing levels are implemented:
per-block, whole-run and optional end-to-end (including generation, data movement
and setup). Parallel benchmarks additionally collect wall-clock execution time and
worker-thread CPU time where available. *Source: BR §7.2.*

## 7. Measurement detail and overhead

**REQ-BENCH-08 (Required).** Aggregate low-overhead measurement is the default;
detailed per-block timing is configurable and opt-in; measurement overhead is
estimated and reported; raw measurements and overhead estimates are preserved
independently and overhead is never silently subtracted; timing overhead is
investigated and tested especially for small block sizes. *Source: BR §7.3.*

Metric names, units, status labels, percentile methods and aggregation rules are
owned by [METRICS_REFERENCE](METRICS_REFERENCE.md).

## 8. Parallelism

**REQ-BENCH-09 (Required).** Both multiple independent DSP instances running
concurrently and DSP engines performing their own internal multithreaded
processing are supported. Thread counts, CPU affinity and scheduling configuration
are collected where available. *Source: BR §8.*

**REQ-BENCH-10 (Required).** Automatic/default scheduling, user-selectable CPU
affinity, physical-versus-logical core selection where detectable, configurable
worker-thread count and optional OS-permitted CPU isolation are supported.
*Source: BR §8.*

**REQ-BENCH-11 (Required).** Existing scheduling priority is respected by default
and recorded; higher priority is optional, requires explicit opt-in and clear
warnings, checks permissions and gracefully falls back if refused, and must not
unexpectedly alter system scheduling behavior. *Source: BR §8.1.*

Platform-specific mechanisms, availability reporting and stability monitoring are
owned by [PLATFORM_MONITORING](PLATFORM_MONITORING.md).

## 9. Reliability

**REQ-BENCH-12 (Required).** Two reliability approaches are provided: convenience
mode (practical defaults) and controlled/reproducibility mode (stronger controls
and environment checks). Features include warm-up, repeated measurements, outlier
detection, configurable duration, background CPU-load awareness, thermal/frequency
monitoring, environment metadata and measurement-overhead estimation.
*Source: BR §9.*

**REQ-BENCH-13 (Required).** Environmental instability (thermal, frequency,
background load) is warned about and recorded in the result; measurements are not
silently discarded and the run is not claimed as fully controlled. The v1.0
decision is to warn, not automatically reject or repeat tests. *Source: BR §9.*

## 10. Correctness validation

**REQ-BENCH-14 (Required).** Correctness validation is separate from timed
performance measurement and never distorts primary timing. *Source: BR §10.*

**REQ-BENCH-15 (Required).** Three independently selectable validation levels are
provided: Basic (NaN, infinity, invalid output, obvious failures), Standard (Basic
plus reference-output comparisons where available), Strict (Standard plus
adapter-specific checks and more extensive input/output testing). *Source: BR §10.1.*

**REQ-BENCH-16 (Required).** Output checks and reference sources include NaN/Inf
detection, unexpected clipping or out-of-range values where applicable, reference
comparisons with configurable tolerances, detection of expected output changes,
adapter custom checks, adapter-provided references, workload reference files,
deterministic reference generation for built-in workloads, absolute and relative
tolerances, and direct comparison of two implementations with one designated as
reference. *Source: BR §10.2.*

Derived design for reference comparisons: a sample passes when
`|actual - reference| <= atol + rtol * |reference|`. NaN, Inf and out-of-range
outputs fail unless that check explicitly declares them expected. The absolute and
relative tolerances used for each check are recorded with its validation outcome.

SDK hooks for validation are owned by
[SDK_SPECIFICATION §7](SDK_SPECIFICATION.md#7-validation-through-the-sdk); outcome
encoding is owned by [RESULTS_FORMAT](RESULTS_FORMAT.md).

## 11. Failure and integrity behavior during a run

Workload failure, infrastructure failure and measurement warning are distinct;
completed measurements are preserved; independent tests continue where safe;
incomplete runs are marked incomplete and never presented as successful complete
benchmarks. *Source: BR §14.* Execution containment is owned by
[ARCHITECTURE §6](ARCHITECTURE.md#6-failure-containment).

## 12. Configuration mapping

Every setting named in this document — dimensions, profile, workload, generator
parameters, timing/measurement detail, parallelism, reliability, validation and
execution settings — appears in the portable versioned configuration format owned
by [RESULTS_FORMAT](RESULTS_FORMAT.md#6-config-document). A config reused on another
machine must surface unsupported capabilities and environment differences instead
of silently changing the test (BR §13).
