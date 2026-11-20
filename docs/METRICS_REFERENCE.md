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
| MET-11 | Performance | Throughput | samples/s and/or blocks/s | measured or estimated |
| MET-12 | Performance | Real-time factor | dimensionless ratio | measured or estimated |
| MET-13 | Resource | CPU utilization | percent | measured (black-box) or estimated |
| MET-14 | Resource | Memory usage | bytes (current and/or peak) | measured/estimated by platform |
| MET-15 | Parallelism | Scaling efficiency | dimensionless ratio, 0–1 ideal | estimated |
| MET-16 | Real-time | Deadline misses | count and rate | measured when deadlines exist, otherwise not_applicable |
| MET-17 | Measurement quality | Benchmark overhead estimate | seconds and percent | estimated |

## 3. Definitions and formulas

Derived design notes follow; the requirement to report the metric is from BR §7.1.

### 3.1 Aggregates over durations

Given *n* observed durations `d_i` (per-block samples or run repetitions, as
configured):

- **Mean** = `sum(d_i) / n`.
- **Median** = middle order statistic (average of two middle values for even *n*).
- **Minimum / Maximum** = `min(d_i)`, `max(d_i)`.
- **Standard deviation** = sample standard deviation `sqrt(sum((d_i - mean)^2) / (n - 1))`.
- **P90 / P95 / P99 / P99.9** = corresponding quantiles of the ordered sample.
  Derived design: use the nearest-rank method and record the method in metadata so
  comparisons use a consistent definition.

### 3.2 Throughput and real-time factor

- **Throughput** = processed audio frames / DSP processing time, reported in
  samples/s and optionally blocks/s. For parallel runs, report both per-instance
  and aggregate throughput.
- **Audio duration** = frames / sample rate.
- **Real-time factor (RTF)** = DSP processing time / audio duration. RTF < 1 means
  processing is faster than real time. RTF is meaningful for offline runs as a
  capacity indicator and is not evidence of live-audio deadline compliance.

### 3.3 Scaling efficiency

- **Scaling efficiency** compares parallel capacity to ideal linear scaling.
  Derived design: `E(n) = throughput(n) / (n * throughput(1))` for the same work,
  alternatively `T(1) / (n * T(n))` for fixed total work. The selected formula and
  worker count are recorded with the metric. Reports must state whether scaling is
  over independent instances or engine-internal threads (BR §8).

### 3.4 Deadline misses

A deadline miss is an unfinished processing unit at its configured deadline.
Derived design: in pure offline mode with no deadlines configured, MET-16 is
`not_applicable`; in simulated real-time or live modes it is `measured` when the
deadline source is available.

### 3.5 Overhead estimate

MET-17 estimates the cost of measurement itself (timer calls, per-block hooks,
instrumentation and, where separable, IPC). It is reported as `estimated` and
stored independently of raw measurements. It is never silently subtracted from
MET-01–MET-10 (BR §7.3). Overhead must be tested particularly at small block sizes.

## 4. Aggregation and raw data rules

**REQ-MET-03 (Required).** Raw measurements and overhead estimates are preserved
independently; estimated overhead is never silently subtracted from reported
measurements. *Source: BR §7.3.*

**REQ-MET-04 (Required).** Aggregate mode is the default; detailed per-block
collection is configurable. *Source: BR §7.3; collection behavior in
[BENCHMARK §7](BENCHMARK_SPECIFICATION.md#7-measurement-detail-and-overhead).*

Derived design:

- Aggregates are computed from retained raw samples, never from a separate
  in-loop accumulator that discards them when detail is requested.
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

1. Establish a baseline distribution from repeated runs of the same build; retain
   all samples.
2. Compute the relative change in the chosen aggregate metric (for example, mean
   or P95) against the configured percentage threshold.
3. Apply a significance test or bootstrap confidence interval over the two sample
   sets before labeling a change. The exact test is an implementation choice, but
   it must be recorded in the comparison metadata.
4. Run only when both threshold and significance criteria hold. A difference that
   is significant but below the threshold is a raw difference; a difference above
   the threshold but not significant is within normal variability.
5. Attach comparability warnings when build metadata, machine metadata or
   environment snapshots differ materially (BR §11.3, §13).
6. Outlier handling is configurable and reported; outliers are not silently
   discarded, consistent with the warn-not-reject policy in BR §9.

## 7. Method metadata

Every metric-bearing result records: percentile method, aggregation window,
timing level used, number of samples, source kind, status labels, worker/instance
counts where relevant, and the regression test/threshold when a comparison is
present. This metadata is serialized per [RESULTS_FORMAT](RESULTS_FORMAT.md).
