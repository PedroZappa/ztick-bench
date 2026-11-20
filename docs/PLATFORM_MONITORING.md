# Platform Monitoring Specification

| | |
| --- | --- |
| **Status** | Technical specification — v1.0 baseline complete |
| **Source of truth** | [DECISIONS.md](DECISIONS.md) BR §8, §9 |
| **Authoritative for** | System information inventory, runtime monitoring, CPU affinity/topology/scheduling/priority, reliability modes at the platform level, availability reporting |
| **Not authoritative for** | When measurements are taken ([BENCHMARK](BENCHMARK_SPECIFICATION.md)), metric formulas ([METRICS](METRICS_REFERENCE.md)), result encoding ([RESULTS](RESULTS_FORMAT.md)) |

---

## 1. Requirement base

**REQ-PLAT-01 (Required).** The application collects the proposed system
information where available and monitors/records CPU frequency before, during and
after a run, temperature, thermal throttling indicators and frequency-scaling /
power-management information where available. *Source: BR §9.*

**REQ-PLAT-02 (Required).** Thermal, frequency, background-load and other
environmental instability produces a user warning and is recorded in the result;
measurements are not silently discarded and the run is not claimed as fully
controlled. The v1.0 decision is to warn rather than automatically reject or
repeat tests. *Source: BR §9.*

**REQ-PLAT-03 (Required).** Platform-dependent capabilities must be reported as
`unsupported` or `unavailable` rather than silently omitted. *Source: BR §17.*
This document defines the availability states used by the abstractions in
[ARCHITECTURE §7](ARCHITECTURE.md#7-platform-abstraction-boundary).

## 2. System information inventory

Collect where available (BR §9):

| Item | Examples of source | Availability condition |
| --- | --- | --- |
| CPU model and architecture | CPUID / `/proc/cpuinfo`, `uname` | CPU identification exposed |
| Physical and logical core counts | topology enumeration | SMT/topology visible to OS |
| CPU frequency and scaling behavior | sysfs/cpufreq, platform APIs | Frequency scaling driver exposed |
| Cache information | CPUID / sysfs | Cache topology exposed |
| SIMD capabilities | CPU feature flags | Architecture exposes features |
| RAM information | `/proc/meminfo`, platform APIs | Memory size reported |
| OS and kernel information | `uname`, OS APIs | Always available on supported OS |
| Audio backend and device information | audio subsystem APIs | Relevant to the selected benchmark mode; `not_applicable` in pure offline runs |
| CPU affinity and scheduling configuration | scheduler APIs | Process/thread scheduling visible |
| Other relevant environment metadata | platform-specific | As detectable |

Each field is reported with an explicit availability state; an undetectable field
is `unavailable`, not absent.

## 3. Runtime monitoring

Collect and record during a run where available (BR §9):

| Signal | When sampled | Role |
| --- | --- | --- |
| CPU frequency | before, during and after | Detect scaling within a run |
| CPU temperature | before, during and after | Detect thermal pressure |
| Thermal throttling indicators | throughout | Warn and record condition |
| Frequency scaling / power-management state | throughout | Explain variability |
| Background CPU load | before and during | Awareness for controlled comparisons |

Signals that are unavailable on a platform are marked `unavailable`; signals that
do not apply to the mode are marked `not_applicable`. Warning conditions become
measurement warnings attached to the result ([RESULTS](RESULTS_FORMAT.md#9-failure-and-warning-encoding)).

## 4. CPU configuration and scheduling

**REQ-PLAT-04 (Required).** Automatic/default scheduling, user-selectable CPU
affinity, physical-versus-logical core selection where detectable, configurable
worker-thread count and optional OS-permitted CPU isolation are supported.
*Source: BR §8.*

**REQ-PLAT-05 (Required).** Thread counts, CPU affinity and scheduling
configuration are collected where available. *Source: BR §8.*

**REQ-PLAT-06 (Required).** Existing scheduling priority is respected by default
and recorded in metadata. Higher priority is optional and requires explicit user
opt-in with clear warnings about system responsiveness and measurement bias;
permissions are checked and the request gracefully falls back if the OS refuses;
system scheduling behavior must not be unexpectedly altered. *Source: BR §8.1.*

Derived design of the control surface:

| Control | Default | User override | Failure behavior |
| --- | --- | --- | --- |
| Affinity | OS default | Explicit CPU set; physical/logical selection | Warn and record if the OS rejects the mask |
| Worker count | Automatic | Configured count | Report actual count used |
| CPU isolation | Off | Request where OS permits | Warn and continue if not permitted |
| Priority | Inherit existing priority, record it | Explicit opt-in only | Check permissions, warn, fall back to inherited priority |

Warning text for priority changes must mention reduced system responsiveness and
possible measurement bias before consent is accepted.

## 5. Reliability modes at the platform layer

Two approaches are required (BR §9):

| Mode | Platform behavior |
| --- | --- |
| Convenience | Practical defaults; best-effort monitoring; warnings for detected instability |
| Controlled/reproducibility | Stronger environment checks, explicit affinity/priority configuration, fuller pre/during/post sampling and comparability metadata |

Both modes share the same warn-and-record policy: neither silently discards
measurements nor declares a fully controlled run when instability was observed.
Mode selection and its effects on warm-up/repetitions/duration are specified in
[BENCHMARK §9](BENCHMARK_SPECIFICATION.md#9-reliability).

## 6. Availability states

The platform abstraction reports one of:

| State | Meaning |
| --- | --- |
| `value` | Supported and successfully read |
| `unavailable` | Supported in principle but the value could not be obtained now |
| `unsupported` | Not implemented for this OS/backend/mode |
| `not_applicable` | Meaningless for the selected benchmark mode |

These states map to the metric value statuses in
[METRICS §1](METRICS_REFERENCE.md#1-value-statuses) and are serialized using
[RESULTS_FORMAT](RESULTS_FORMAT.md#31-value-statuses). Derived design mapping:

| Platform state | Metric value status |
| --- | --- |
| `value`, directly observed | `measured` |
| `value`, derived from other signals | `estimated` |
| `unavailable` | `unavailable`, with reason |
| `unsupported` | `unavailable` with reason, or `not_applicable` when the capability is undefined for the selected mode |
| `not_applicable` | `not_applicable`, with reason |

## 7. Platform coverage and roadmap

- **Linux** is the full v1.0 implementation target (BR §1, §17).
- **macOS** is second and **Windows** later; their implementations use the same
  abstraction and report `unsupported` for capabilities they cannot provide.
- Platform expansion is phased in [ROADMAP](ROADMAP.md).

Platform-specific syscalls, file paths and kernel interfaces stay inside this
layer; no OS-specific code belongs in the portable core or front ends
(BR §15, REQ-ARCH-02).
