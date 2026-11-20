# Metrics Reference

| | |
| --- | --- |
| **Status** | Technical specification — v1.0 baseline complete |
| **Source of truth** | [DECISIONS.md](DECISIONS.md) BR §7.1, §11.3 |
| **Authoritative for** | Metric definitions, formulas, units, value statuses, aggregation rules, comparison/regression statistics |
| **Not authoritative for** | When metrics are collected ([BENCHMARK](BENCHMARK_SPECIFICATION.md)), platform collection mechanisms ([PLATFORM](PLATFORM_MONITORING.md)), JSON encoding ([RESULTS](RESULTS_FORMAT.md)) |

---

## 1. Value statuses

**REQ-MET-01 (Required).** Every reported metric distinguishes measured,
estimated, unavailable and not-applicable values. *Source: BR §7.1.* This applies
in particular when comparing black-box process monitoring with instrumented DSP
measurements.

| Status | Meaning | Example |
| --- | --- | --- |
| `measured` | Directly observed by instrumentation or monitoring | Per-block duration from host timer |
| `estimated` | Derived or approximated | Timing overhead estimate, black-box CPU approximation |
| `unavailable` | The platform/integration could not provide it | CPU temperature without a sensor |
| `not_applicable` | Undefined for this benchmark type or mode | Deadline misses in pure offline mode without deadlines |

A `not_applicable` value is not the same as an omission: the metric is present in
the result with its status. Serialization is owned by
[RESULTS_FORMAT](RESULTS_FORMAT.md#31-value-statuses).

## 2. Initial metric catalog

**REQ-MET-02 (Required).** All metrics below are in the first release, subject to
applicability and availability for the benchmark type. *Source: BR §7.1.*

| ID | Category | Metric | Unit / form | Default status by source |
| --- | --- | --- | --- | --- |
| MET-01 | Timing | Execution time | seconds (duration of measured processing) | measured |
| MET-02 | Timing | Mean | same unit as MET-01 | measured |
| MET-03 | Timing | Median | same unit as MET-01 | measured |
| MET-04 | Timing | Minimum | same unit as MET-01 | measured |
| MET-05 | Timing | Maximum | same unit as MET-01 | measured |
| MET-06 | Statistical | Standard deviation | same unit as MET-01 | measured |
| MET-07 | Statistical | P90 | same unit as MET-01 | measured |
| MET-08 | Statistical | P95 | same unit as MET-01 | measured |
| MET-09 | Statistical | P99 | same unit as MET-01 | measured |
| MET-10 | Statistical | P99.9 | same unit as MET-01 | measured |
| MET-11 | Performance | Throughput | frames/s (canonical), samples/s and blocks/s (derived) | measured or estimated |
| MET-12 | Performance | Real-time factor | dimensionless ratio | measured or estimated |
| MET-13 | Resource | CPU utilization | percent (denominator defined in §3.6) | measured (black-box) or estimated |
| MET-14 | Resource | Memory usage | bytes (current and/or peak) | measured/estimated by platform |
| MET-15 | Parallelism | Scaling efficiency | dimensionless ratio; 1 = ideal linear (may exceed 1) | estimated |
| MET-16 | Real-time | Deadline misses | count and rate (misses / deadlines) | measured when deadlines exist, otherwise not_applicable |
| MET-17 | Measurement quality | Benchmark overhead estimate | seconds and percent of DSP processing time | estimated |

## 3. Definitions and formulas

Derived design notes follow; the requirement to report the metric is from BR §7.1.

### 3.1 Aggregates over durations

Durations are aggregated within one population only: either per-block durations
from a single run or run-repetition durations. Never mix the two populations in one
aggregate; §7's "number of samples" records which population was used. Let `n` be
the size of that population and `d_(1) <= ... <= d_(n)` its ascending order
statistics.

- **Mean** = `sum(d_i) / n`, defined for `n >= 1`.
- **Median** = `d_((n+1)/2)` for odd `n`, `(d_(n/2) + d_(n/2+1)) / 2` for even `n`.
  This is a distinct definition from nearest-rank `P50`; P50 is not reported.
- **Minimum / Maximum** = `d_(1)`, `d_(n)`.
- **Standard deviation** = sample standard deviation
  `sqrt(sum((d_i - mean)^2) / (n - 1))`, defined only for `n >= 2`. For `n = 1` the
  value is `not_applicable` with reason `insufficient_samples`; an empty population
  is rejected.
- **P90 / P95 / P99 / P99.9** = nearest-rank quantiles. Derived design: sort
  ascending and use `rank = ceil(q * n)` for `0 < q <= 1` with no interpolation, so
  the result is an observed sample. For `n < 1000`, P99.9 equals the maximum. Record
  `n` and the method in metadata so comparisons use the same definition.

### 3.2 Throughput and real-time factor

- **Throughput** = processed audio frames / DSP processing time. The canonical unit
  is frames/s; for `C` channels and block size `B`, `samples/s = frames/s * C` and
  `blocks/s = frames/s / B`. "Samples" means per-channel samples. The denominator
  is the DSP processing time of the selected interval — sum of block durations or
  whole-run wall time — and is recorded with its source kind; see
  [BENCHMARK §6–§7](BENCHMARK_SPECIFICATION.md#6-execution-lifecycle-and-timing-boundaries).
  Worker CPU time is reported separately and is not summed across instances into an
  aggregate rate. For parallel runs, report per-instance rates and an aggregate
  defined as total frames / wall-clock time of the run.
- **Audio duration** = frames / sample rate, requiring `sample_rate > 0`; "frames"
  are per-channel frames. With oversampling `O`, record whether the workload rate
  `fs` or the internal rate `O * fs` is used.
- **Real-time factor (RTF)** = DSP processing time / audio duration; equivalently
  `RTF = sample_rate / throughput_frames_per_s` when both use the same processing
  time. `RTF = 1` is exactly real time, `< 1` faster, `> 1` slower. RTF is a capacity
  indicator for offline runs, not evidence of live-audio deadline compliance; for
  `N` instances, per-instance `RTF < 1` does not by itself guarantee system capacity.

### 3.3 Scaling efficiency

- **Scaling efficiency** compares parallel capacity to ideal linear scaling.
  Derived design: hold the **total** work fixed and partition it over `n` workers.
  With wall-clock times `T(1)` and `T(n)`, `E(n) = T(1) / (n * T(n))`;
  equivalently, `E(n) = throughput(n) / (n * throughput(1))` when both throughputs
  use the same wall-clock denominator. `E = 1` is ideal linear scaling; `E > 1` is
  superlinear (for example from cache effects) and is not an error. The metric is
  undefined when `T(n) = 0` or `throughput(1) = 0`, and is reported as
  `not_applicable` in that case. Replicating the same work on every instance makes
  `E` identically 1 and must not be reported as scaling. The selected formula,
  worker count, and whether scaling is over independent instances or engine-internal
  threads are recorded with the metric (BR §8).

### 3.4 Deadline misses

A deadline miss is an unfinished processing unit at its configured deadline.
Derived design: in pure offline mode with no deadlines configured, MET-16 is
`not_applicable`; in simulated real-time or live modes it is `measured` when the
deadline source is available. The **rate** is `misses / deadlines` in the measured
window, not misses per second; the window is recorded with the metric.

### 3.5 Overhead estimate

MET-17 estimates the cost of measurement itself (timer calls, per-block hooks,
instrumentation and, where separable, IPC). It is reported as `estimated` and
stored independently of raw measurements. It is never silently subtracted from
MET-01–MET-10 (BR §7.3). Overhead must be tested particularly at small block sizes.
The percent form is `overhead_seconds / DSP processing time * 100` for the same
interval; the denominator is recorded in metadata.

### 3.6 Percentage quantities

- **CPU utilization (MET-13)** = `CPU time / wall time * 100` over the measurement
  window. Record whether it is normalised to one core (`0–100%`) or to all logical
  cores (may exceed 100% under parallelism); the two must not be mixed in
  comparisons.
- Deterministic aggregates over measured samples remain `measured`; only
  approximations or inferences are `estimated` (§1).

## 4. Aggregation and raw data rules

**REQ-MET-03 (Required).** Raw measurements and overhead estimates are preserved
independently; estimated overhead is never silently subtracted from reported
measurements. *Source: BR §7.3.*

**REQ-MET-04 (Required).** Aggregate mode is the default; detailed per-block
collection is configurable. *Source: BR §7.3; collection behavior in
[BENCHMARK §7](BENCHMARK_SPECIFICATION.md#7-measurement-detail-and-overhead).*

Derived design:

- "Raw basis" means the retained durations from which aggregates are computed:
  every per-block duration in block-level mode, or every repetition duration in
  run-level mode. Aggregates are computed from that retained population, never from
  a separate in-loop accumulator that discards it. Aggregate mode may release the
  raw basis after statistics are computed; detail mode also serializes it.
- Each metric record carries its source kind (instrumented DSP, host wall clock,
  worker CPU time, process monitor), status and collection method reference.
- Missing platform data becomes `unavailable`; undefined combinations become
  `not_applicable`. Silently dropping a metric is not permitted (BR §17).

## 5. Availability by measurement level

| Metric group | Instrumented DSP | Black-box process monitoring |
| --- | --- | --- |
| MET-01–MET-10 | Primary, `measured` when host or adapter timing is available | Approximations must be `estimated` or `unavailable` |
| MET-11–MET-12 | `measured` from processing time and frames | `estimated` from process activity where possible |
| MET-13–MET-14 | Process-level values, `measured`/`estimated` per platform | Primary source |
| MET-15 | `estimated` from configured worker/instance counts and timings | `estimated` only if per-worker data is observable |
| MET-16 | `measured` when deadlines/instrumentation exist | `unavailable` unless the runtime exposes deadline events |
| MET-17 | `estimated` | `estimated` where IPC/monitoring cost is observable |

Both levels may appear in one result but must remain labeled separately (BR §2.2).

## 6. Comparison and regression statistics

**REQ-MET-05 (Required).** Regression detection supports configurable percentage
thresholds, calculates statistical significance before reporting, avoids treating
small differences within normal variability as meaningful, preserves underlying
measurements and comparison context, warns on non-comparable environments, and
distinguishes raw performance differences from genuine regressions. *Source: BR §11.3.*

Comparison levels (BR §11.2) are run-to-run, build-to-build and
machine-to-machine. Statistical handling is owned by this document; storage of
comparison records is owned by [RESULTS_FORMAT](RESULTS_FORMAT.md).

Derived design:

1. Establish a baseline distribution from repeated **runs** of the same build;
   retain all samples. Runs, not individual per-block samples, are the independent
   unit for significance testing because block samples are autocorrelated.
2. For the chosen aggregate metric `m` (for example, mean or P95), compute the
   relative change `delta_percent = (m_candidate - m_baseline) / m_baseline * 100`,
   requiring `m_baseline != 0`; otherwise the comparison is `not_applicable`. Record
   metric polarity: lower-is-better for durations and RTF, higher-is-better for
   throughput. The threshold applies to `abs(delta_percent)`.
3. Apply a significance test or bootstrap confidence interval over the two
   run-level sample sets with a recorded significance level `alpha` and a
   one- or two-sided choice. The exact test is an implementation choice, but test,
   alpha, sidedness and the metrics tested must be recorded in the comparison
   metadata; adjust or disclose multiplicity when several metrics or test points
   are compared.
4. Classify the result: both threshold and significance criteria hold and the
   change is adverse under the recorded polarity → `regression`; both hold and the
   change is favourable → `improvement`; significant but below threshold →
   `raw_difference`; threshold-only or neither → `within_variability`. "No
   significant difference detected" is not proof of equivalence.
5. Attach comparability warnings when build metadata, machine metadata or
   environment snapshots differ materially (BR §11.3, §13).
6. Outlier handling is configurable and reported; outliers are not silently
   discarded, consistent with the warn-not-reject policy in BR §9.

## 7. Method metadata

Every metric-bearing result records: percentile method, aggregation window (the
population — per-block samples of one run or run repetitions — from which
aggregates were computed), timing level used, number of samples, sample rate and
channel count for throughput/RTF, source kind, status labels, worker/instance
counts where relevant, and the regression test/threshold when a comparison is
present. This metadata is serialized per [RESULTS_FORMAT](RESULTS_FORMAT.md).
