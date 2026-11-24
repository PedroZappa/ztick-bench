# Results and Configuration Format

| | |
| --- | --- |
| **Status** | Technical specification — v1.0 baseline complete |
| **Source of truth** | [DECISIONS.md](DECISIONS.md) BR §11–§14 |
| **Authoritative for** | JSON result schema, config schema, CSV mapping, metadata attachment, portability checks, status/failure/comparison encoding, atomic writes |
| **Not authoritative for** | Metric definitions/formulas ([METRICS](METRICS_REFERENCE.md)), system-info contents ([PLATFORM](PLATFORM_MONITORING.md)), execution behavior ([BENCHMARK](BENCHMARK_SPECIFICATION.md)) |

---

## 1. Format requirements

**REQ-RES-01 (Required).** JSON is the canonical complete result format; CSV is
supported for convenient data analysis; benchmark sessions can be exported and
imported; build, machine, workload and environment metadata are attached to each
run; JSON benchmark configurations are portable and versioned. *Source: BR §12.3.*

**REQ-RES-02 (Required).** SQLite is deferred to a later implementation for local
historical storage/querying and is not a first-release requirement. *Source: BR §12.3.*

**REQ-RES-03 (Required).** Workload and benchmark configuration and relevant
settings are snapshotted into results; a config reused on another machine must
identify unsupported capabilities and environment differences rather than silently
changing the test. *Source: BR §4.3, §13.*

Field names and JSON shapes below are **Derived design**; the requirements are the
ledger statements cited.

## 2. Versioning

Every document carries:

| Field | Meaning |
| --- | --- |
| `schema_version` | Version of this results/config schema |
| `sdk_version` | Version of the integration SDK used |
| `config_version` | Version of the portable config format |

A reader must refuse to misinterpret a newer major schema version rather than
guessing. Unknown optional fields are ignored with a warning (Derived design), but
unknown required fields make the document unreadable and are reported explicitly.

## 3. Result document structure

Top-level object (Derived design):

```jsonc
{
  "schema_version": "1.0.0",
  "sdk_version": "1.0.0",
  "session": { "id": "...", "started_at": "...", "ended_at": "..." },
  "config_snapshot": { /* full portable config, see §6 */ },
  "build": { /* §4.1 */ },
  "machine": { /* §4.2 */ },
  "environment": { /* §4.3 */ },
  "runs": [ /* §5 */ ],
  "comparisons": [ /* §10 */ ],
  "warnings": [ /* §9 */ ],
  "failures": [ /* §9 */ ],
  "status": "complete | incomplete"
}
```

### 3.1 Value statuses

Metric and platform values use the four statuses defined in
[METRICS §1](METRICS_REFERENCE.md#1-value-statuses):

```jsonc
{ "id": "MET-09", "name": "P99",
  "value": 0.000123, "unit": "s",
  "status": "measured",
  "source": "instrumented_dsp",
  "method": "nearest_rank" }
```

`unavailable` and `not_applicable` records include a reason string and are present
rather than omitted.

## 4. Attached metadata

### 4.1 Build metadata

Record where discoverable (BR §11.1): git commit hash, compiler and compiler
version, optimization flags, build configuration and relevant toolchain details.
Missing items are `unavailable`, not omitted.

### 4.2 Machine metadata

Contains the system-inventory fields owned by
[PLATFORM_MONITORING §2](PLATFORM_MONITORING.md#2-system-information-inventory),
each with an availability state. This is the basis for machine-to-machine
comparability warnings.

### 4.3 Workload and environment metadata

- **Workload:** level (processor/chain/engine), processor identifiers, chain order,
  generator type and fixed parameters, integration identifier and version.
- **Environment:** affinity, scheduling configuration, priority (including
  inherited priority), worker/instance counts, reliability mode, and runtime
  monitoring signals from
  [PLATFORM_MONITORING §3](PLATFORM_MONITORING.md#3-runtime-monitoring).
- **Execution:** isolation mode (in-process/out-of-process), SDK version checks and
  capability negotiation outcome (BR §6.3, §5.1).

### 4.4 Metadata attachment behavior (Derived design)

The `ztick_results` module builds and attaches the §4 sections through an
availability-aware field model. Each field is a record `{ "status", "value",
"reason" }`; all three members are always present. `status` is one of `value`,
`unavailable`, `unsupported` or `not_applicable`, matching the platform states in
[PLATFORM §6](PLATFORM_MONITORING.md#6-availability-states). A non-value status
has an empty `value` and a non-empty `reason`, so an undetectable item is
reported rather than omitted (REQ-RES-01, BR §17).

Build metadata is captured when the binary is configured: git commit when Git and
a commit are available, C++ compiler and version, optimization/warning flags,
build configuration and project version. Any undiscoverable item is recorded as
`unavailable` with a reason rather than dropped (BR §11.1). Machine fields come
from the platform inventory through the adapter in `ztick_platform`, one field
per [PLATFORM §2](PLATFORM_MONITORING.md#2-system-information-inventory) item,
preserving each availability state.

Workload and environment sections are projected from the config snapshot:
integration identity, workload level, processors, generator and dimensions for
workload; isolation, duration mode, repetitions, workers, affinity and
measurement detail for environment. Fields absent from the snapshot become
`unavailable` records. Attaching metadata writes only the `build`, `machine` and
`environment` sections of a §3 document; it never changes the config snapshot or
per-run records.

**Comparability checks.** Comparing two result documents walks matching
qualified field names in `build`, `machine` and `environment`. Equal value fields
agree. A differing value, a differing status, or a value on one side against a
non-value status on the other produces a comparability warning record with
`category`, `field`, `left_status`, `left_value`, `right_status`, `right_value`
and `message`. Identical non-value statuses do not warn, and fields absent from
both sections are ignored. The warning array is suitable for the
`comparability_warnings` field of a §10 comparison record and is also the basis
for the environment comparability findings in §7. A non-object document is
reported as incomparable rather than silently treated as equal (BR §11.2, §11.3).

## 5. Per-run records

Each run contains (Derived design):

| Field | Contents |
| --- | --- |
| `id`, `test_point` | Identifier and dimension values used |
| `config_snapshot_ref` | Reference to the frozen config for this run |
| `timing` | `per_block`, `whole_run`, optional `end_to_end`; raw samples and aggregate results |
| `metrics` | Metric records from [METRICS §2](METRICS_REFERENCE.md#2-initial-metric-catalog) with statuses |
| `overhead_estimates` | Separate from raw measurements; never pre-subtracted |
| `validation` | Per-check outcomes, level used, references, tolerances |
| `execution` | Isolation mode and actual worker/affinity configuration |
| `warnings`, `failures` | Conditions classified per §9 |
| `status` | `complete`, `incomplete`, or `failed` |

Raw per-block samples are retained and serialized when detail collection is
enabled; in aggregate mode the configured raw basis
([METRICS §4](METRICS_REFERENCE.md#4-aggregation-and-raw-data-rules)) is retained
long enough to compute the aggregates and need not be serialized. Overhead estimates
are always a separate field (BR §7.3).

Derived token set: configuration may request `auto`, `in_process` or
`out_of_process`; a run record stores the selected `in_process` or
`out_of_process` mode when a workload was attempted, never `auto`. These tokens map to the public
`ztick_execution_mode` defined by [SDK](SDK_SPECIFICATION.md#1-purpose-and-conformance) (BR §6.3).

For out-of-process runs the IPC transport carries only the final workload status after
the timed work has completed; launch, wait and transport are infrastructure activity
outside the DSP timing interval. Process isolation reduces crash impact but is not a
complete security sandbox (BR §6.3).

## 6. Config document

**REQ-RES-04 (Required).** Portable JSON configuration includes SDK and config
schema versions, integration identifier and version, workload and signal
parameters, sample rate, block size, channels, instances, oversampling,
measurement and validation settings, profile and duration settings, and relevant
execution settings. *Source: BR §13.*

Derived top-level shape:

```jsonc
{
  "config_version": "1.0.0",
  "sdk_version": "1.0.0",
  "integration": { "id": "...", "version": "..." },
  "workload": { "level": "processor", "processors": [], "generator": {} },
  "dimensions": {
    "sample_rate_hz": 48000, "block_size": 128, "channels": 2,
    "oversampling": 1, "instances": 1
  },
  "execution": { "duration_mode": "iterations", "iterations": 1000,
                 "repetitions": 3, "warmup": {}, "isolation": "auto" },
  "profile": { "name": "Quick" },
  "measurement": { "detail": "aggregate", "end_to_end": false,
                   "timing_levels": ["per_block", "whole_run"] },
  "validation": { "level": "Basic", "tolerances": {}, "references": [] },
  "parallelism": { "workers": "auto", "affinity": "auto" }
}
```

Profiles, dimension values and generator parameters are defined semantically by
[BENCHMARK_SPECIFICATION](BENCHMARK_SPECIFICATION.md#4-benchmark-dimensions).

**Design constraint (BR §21, Q47 deferral):** the config format remains compatible
with a future suite layer. No suite fields or commands are added to v1.0, but a
future suite can reference multiple config documents without redesigning this
schema.

### 6.1 Config reader behavior (Derived design)

A v1.0 reader requires every top-level member shown above. `config_version` must
equal the schema version `1.0.0`; `sdk_version` must be a numeric
`major.minor.patch` token accepted by
[SDK_SPECIFICATION §2](SDK_SPECIFICATION.md#2-versioning-and-compatibility).
Missing, mistyped or unsupported version tokens are rejected, and a rejected
document is never applied partially. `integration.id` and `integration.version`
must be non-empty strings. Semantic validation of the remaining sections belongs
to the component that consumes each section; this reader owns only document
shape, version tokens and integration identity.

## 7. Portability checks

When a config is loaded on another machine, the host compares required
capabilities against the adapter's declarations and its own environment:

1. Integration present and SDK version compatible.
2. Requested formats, layouts, dimensions and parameters supported.
3. Execution settings (affinity count, isolation availability, priority) possible.
4. Validation references resolvable.

Differences are reported as explicit `unsupported`/`unavailable` findings and as
environment comparability warnings. The test must not be silently altered to fit
the new machine (BR §13, REQ-PROJ-05).

## 8. CSV representation

CSV is a convenience projection, not the canonical format (BR §12.3).

- One row per test point or metric sample, as selected at export.
- Columns include run identifier, dimension values, metric identifier, value,
  unit, status, source and warning/failure markers.
- Status and source columns are mandatory so measured/estimated/unavailable/N-A
  distinctions survive export.
- Raw per-block series and complex metadata remain in JSON; CSV export states
  which fields were omitted.

### 8.1 CSV projection behavior (Derived design)

An export validates the result document with §9.1 before projecting it. The v1.0
header is fixed and appears on every export:

```text
session_id,run_id,test_point,metric_id,metric_name,value,unit,status,source,method,reason,warning_count,failure_count,run_status,omitted_fields
```

`per_test_point` emits one row per metric record using its aggregate value;
`per_metric_sample` emits one row per `samples` entry, falling back to the
aggregate when a record has no samples. A scalar sample supplies the value, and
an object sample may override `value`, `status`, `source` or `method`. A run with
no metrics still produces a row so its test point is not lost. `test_point`
flattens dimension values as `key=value` pairs separated by semicolons in JSON
member order.

Cells follow RFC 4180 quoting. Numbers and booleans retain their JSON text, so
identifier, value, unit, status and source cells agree directly with the
canonical document. `status`, `source` and `run_status` are always present, with
an empty value cell for `unavailable` and `not_applicable` metrics and their
`reason` carried in the `reason` column. The `omitted_fields` column names raw
per-block series and complex metadata; those fields remain in JSON only. CSV
files are written through the atomic mechanism in §9.1 and never replace JSON.

## 9. Failure and warning encoding

**REQ-RES-05 (Required).** Workload failure, infrastructure failure and
measurement warning are distinguished; completed measurements are preserved;
failures and context are recorded; incomplete runs are marked incomplete and never
presented as successful complete benchmarks; result files are not corrupted when a
component fails. *Source: BR §14.*

| Category | Encoding | Example |
| --- | --- | --- |
| Workload failure | Per-run `failure` with adapter/crash context | Adapter crash, invalid data |
| Infrastructure failure | Session- or run-level `failure` with load/monitor context | Plugin cannot load, monitoring unavailable |
| Measurement warning | `warning` attached to affected run(s) | Thermal throttling, unexpected CPU load |

A session with any failed or interrupted run has `status: "incomplete"`; it must
never be rendered as a successful complete benchmark.

### 9.1 Result document writer behavior (Derived design)

A v1.0 result document requires the top-level fields in §3 with the types shown
there. `schema_version` must equal `1.0.0`; `sdk_version` must satisfy
[SDK_SPECIFICATION §2](SDK_SPECIFICATION.md#2-versioning-and-compatibility).
`session.id` must be a non-empty string, `config_snapshot` must be a valid §6
document, and `status` must be `complete` or `incomplete`.

Before encoding, the writer derives the session status from the run records: a
run that records failures while claiming `complete` is marked `incomplete`; any
incomplete or failed run, or any session-level failure, makes the session
`incomplete`. A stored `complete` token is never trusted over that evidence.
Readers validate the stored token but do not silently rewrite stored results.

**Atomic writes (Derived design):** serialize to a unique temporary file in the
target directory, flush and close it, then atomically rename it over the target.
A failure removes the temporary file and never truncates or removes the existing
target.

## 10. Comparison records

Comparison records preserve the underlying measurements and context (BR §11.3):

| Field | Contents |
| --- | --- |
| `baseline`, `candidate` | References to result documents and selected metrics |
| `level` | `run_to_run`, `build_to_build`, `machine_to_machine` (BR §11.2) |
| `threshold_percent` | Configured percentage threshold, applied to the absolute relative change |
| `threshold_status` | `applied` or `not_applicable`, with an explicit reason |
| `statistics` | Significance level `alpha`, one/two-sided choice, test method, iteration/seed details, multiplicity disclosure and metrics tested |
| `significance` | Test/method used and outcome from [METRICS §6](METRICS_REFERENCE.md#6-comparison-and-regression-statistics) |
| `classification` | `raw_difference`, `within_variability`, `regression`, `improvement` |
| `comparability_warnings` | Build/machine/environment mismatches from [§4.4](#44-metadata-attachment-behavior-derived-design) |
| `context` | Copies of relevant metadata so the comparison is self-contained |

**Derived design (P4.8).** The core's raw comparison entry point produces
`classification: raw_difference`, a null `threshold_percent`, and
`threshold_status`, `statistics` and `significance` objects whose status is
`not_applicable`. Metric entries retain both raw values and statuses; `delta` is
`candidate - baseline` and `relative_change_percent` is
`(candidate - baseline) / baseline * 100`, with a null relative field and an
explicit reason when the baseline is zero. A metric present on only one side is
an `unmatched` entry carrying `side` instead of being dropped.

**Derived design (P4.9).** The classified comparison entry point preserves every
P4.8 field and adds the following evidence. The test, alpha, sidedness, seed and
iteration count are recorded in `statistics`; `statistics.multiplicity` is
`unadjusted`, and `statistics.outliers_removed` is zero. Counts are per distinct
metric identifier, not per run pair. The top-level `significance` object carries
the aggregate regression, improvement, raw-difference and within-variability
counts; its status is `not_applicable` when no metric could be tested.

| Metric field | Contents |
| --- | --- |
| `baseline_samples`, `candidate_samples` | All finite run-level values collected for that metric; never per-block samples |
| `baseline_sample_count`, `candidate_sample_count` | Number of retained values on each side |
| `baseline_mean`, `candidate_mean` | Arithmetic means of the retained run-level values |
| `aggregate_delta` | `candidate_mean - baseline_mean` |
| `aggregate_relative_change_percent` | `aggregate_delta / baseline_mean * 100`, or null when the baseline mean is zero |
| `polarity` | `lower_is_better`, `higher_is_better` or `unspecified` |
| `threshold_status`, `threshold_exceeded` | Threshold decision and its explicit reason |
| `significance` | `method`, `alpha`, `sided`, `significant`, `interval_low`, `interval_high` and an explicit status/reason |
| `classification` | Per-metric `regression`, `improvement`, `raw_difference` or `within_variability` |

A metric is a `regression` only when its absolute aggregate relative change meets
the threshold and its significance interval supports the observed adverse
direction under the recorded polarity. A favourable change meeting both criteria
is an `improvement`. A significant change below the threshold, or one whose
threshold is not applicable, is a `raw_difference`; threshold-only and untestable
changes are `within_variability`. Unmatched and unavailable entries carry
`not_applicable` significance and are never reported as regressions. The
overall `classification` follows the METRICS §6 precedence with any regression
dominating. Command behavior is owned by
[PROJECT_SPEC §3.3](PROJECT_SPEC.md#33-cli-command-set).

## 11. Session export/import and storage

- A session export bundles the result document(s), comparison records and config
  snapshots; import reconstructs them without recomputing results.
- CSV may be exported alongside JSON but never replaces it.
- SQLite storage is deferred (REQ-RES-02) and must not appear as a v1.0 dependency
  in build configuration.
