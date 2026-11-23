# DSP Integration SDK Specification

| | |
| --- | --- |
| **Status** | Technical specification — v1.0 baseline complete |
| **Source of truth** | [DECISIONS.md](DECISIONS.md) BR §5; BR §6 for plugin policy |
| **Authoritative for** | C integration ABI, C++ wrappers, adapter contract, version/capability checks, integration packaging |
| **Not authoritative for** | Host discovery/trust/isolation runtime ([ARCHITECTURE](ARCHITECTURE.md)), metric formulas ([METRICS](METRICS_REFERENCE.md)), result schema ([RESULTS](RESULTS_FORMAT.md)) |

**Required** statements trace to the ledger. Concrete names, numeric codes and
struct shapes below are **Derived design** unless marked otherwise.

---

## 1. Purpose and conformance

**REQ-SDK-01 (Required).** The SDK provides a versioned, C-compatible integration
interface with C++ convenience wrappers. The C boundary avoids depending on a
stable C++ ABI across compilers and platforms. *Source: BR §5.1.*

**REQ-SDK-02 (Required).** The SDK defines one common adapter contract covering
capabilities, configuration, workload execution, measurements, validation and
result reporting. *Source: BR §5.1.*

Two conformance forms are required (BR §5.1):

| Form | Integration packaging |
| --- | --- |
| Static | Adapter compiled into ztick-bench via a registration point |
| Dynamic | Adapter provided as a shared library and loaded at runtime |

**REQ-SDK-03 (Required).** SDK version and capability checks must be performed
before an integration is configured or executed. *Source: BR §5.1.*

### 1.1 Frozen public header contract (BR §22)

These names are fixed before the first public header is written. Changing any of
them after Phase 1 is a breaking change requiring explicit approval.

| Item | Frozen value |
| --- | --- |
| Public include root | `<ztick/sdk.h>`, `<ztick/types.h>`, `<ztick/version.h>` |
| C++ wrappers | `<ztick/cpp/adapter.hpp>`, namespace `ztick::sdk` |
| Symbol prefix | `ztick_` for all exported C functions and types |
| Macro prefix | `ZTICK_` for all public macros |
| C subset | C11-compatible public headers; `<stdint.h>` and `<stddef.h>` only; fixed-width integers; no C++ types, exceptions, templates or C++ ABI |
| Handles | Opaque incomplete structs, e.g. `typedef struct ztick_adapter ztick_adapter;`, created and destroyed by SDK functions |
| Struct evolution | Public structs begin with `uint32_t size` and `uint32_t version`; zero-initialize, then set `size = sizeof(struct)` and `version = ZTICK_STRUCT_LAYOUT_VERSION` |
| Visibility | `ZTICK_API` export/import macro (`visibility("default")` on GCC/Clang) |
| Calling convention | Plain C; no platform-specific decorators in public headers |
| Version macros | `ZTICK_SDK_VERSION_MAJOR/MINOR/PATCH` = `1`, `0`, `0`; additive derived `ZTICK_STRUCT_LAYOUT_VERSION` = `1` |

### 1.2 Core type inventory

These types are defined once in `<ztick/types.h>` and used by the core, SDK and
result schema. No other document defines a competing version; other docs link
here instead of redefining them.

| Type | Kind | Purpose |
| --- | --- | --- |
| `ztick_version` | struct | major, minor, patch |
| `ztick_status` | enum | success, unsupported, incompatible, invalid argument, processing failure, validation failure, internal error |
| `ztick_sample_format` | enum | `float32` (mandatory), `float64` (optional) |
| `ztick_channel_layout` | enum | interleaved, planar |
| `ztick_metric_status` | enum | measured, estimated, unavailable, not_applicable |
| `ztick_availability` | enum | value, unavailable, unsupported, not_applicable |
| `ztick_execution_mode` | enum | in_process, out_of_process |
| `ztick_audio_format` | struct | format, layout, sample rate, channels, frames |
| `ztick_parameter_descriptor` | struct | typed parameter metadata: type, range, default, description |
| `ztick_adapter_info` | struct | identity, version, SDK version, declared capabilities |
| `ztick_buffer_view` | struct | non-owning plane pointers, strides, frame count |
| `ztick_timing_sample` | struct | block index and duration |
| `ztick_validation_result` | struct | check outcome, tolerances, reference source |

## 2. Versioning and compatibility

Derived design:

- Version is `(major, minor, patch)`, exposed by compile-time macros and a runtime
  query. The adapter reports the SDK version it was built against.
- **Major** versions must match exactly; a mismatch is an incompatibility and the
  integration must not be executed.
- **Minor** versions are backward-compatible for hosts with a newer minor; a host
  older than the adapter's declared minor must refuse the integration.
- **Patch** differences are compatible.
- New capabilities are additive: an unknown capability bit or parameter type is
  treated as unsupported, never assumed to be supported.

**REQ-SDK-04 (Required).** If a required format, capability or parameter value is
unsupported, the adapter must declare it so the application can explain why a test
cannot run. *Source: BR §5.3, §5.4.*

Compatibility failures are reported as `unavailable`/`unsupported` in results rather
than silently skipped, per BR §17 and
[PROJECT_SPEC §6](PROJECT_SPEC.md#6-cross-cutting-invariants).

**Runtime implementation (Derived design).** For ABI 1.0.0, a public struct passed to
a runtime check must declare `size >= sizeof(struct)` and
`version == ZTICK_STRUCT_LAYOUT_VERSION` (published by `<ztick/version.h>`); a
malformed header returns `ztick_status_invalid_argument`. `ztick_sdk_get_version`
writes only the version components and leaves the caller's `size`/`version` fields
intact. `ztick_sdk_is_compatible` applies the major/minor rules above, ignores patch
differences and reports a malformed header as `invalid_argument`. The registry behind
`ztick_register_adapter` holds non-owning descriptor pointers, requires distinct
identifiers and is thread-safe; descriptors must outlive the host. Registration
validates the `size`/`version` headers of the descriptor, its info/ops tables and both
nested version structs, plus identity strings and SDK compatibility; it creates no
instance, executes no callback and grants no trust (BR §6.2). Status strings are
static, NUL-terminated and never NULL, including for unknown codes. No exception
crosses the C boundary (§8).

## 3. Adapter contract: lifecycle

The contract is stateful and host-driven. Derived design of the states:

| State | Operation | Requirement source |
| --- | --- | --- |
| Inspect | Provide identity, version, SDK version, declared capabilities and metadata **without executing processing** | BR §6.1 |
| Create | Instantiate one adapter instance | BR §5.1 |
| Describe | Return capabilities, typed parameters, supported formats and unsupported configurations | BR §5.3 |
| Configure | Apply validated typed parameters | BR §5.3 |
| Prepare | Accept generated input buffers and any reference data — **outside** the timed interval | BR §4.2, §7.4 |
| Process | Execute one audio block; optionally emit per-block timing samples | BR §7.2 |
| Finish | Drain/flush and finalize instrumented counters | BR §5.1 |
| Validate | Run adapter-provided checks against supplied references — **outside** timing | BR §10 |
| Report | Return measurements with value-status labels and validation outcomes | BR §5.1, §7.1 |
| Destroy | Release all resources | Derived |

An adapter must remain usable for multiple instances and must not assume a single
global state unless it declares that limitation as an unsupported configuration.

## 4. Capabilities and parameters

**REQ-SDK-05 (Required).** Adapters must be able to declare supported sample
formats and channel layouts, supported benchmark capabilities, typed configuration
parameters, parameter types/valid ranges/defaults/descriptions, and unsupported
configurations. *Source: BR §5.3.*

Derived design of the declaration model:

| Declaration | Contents |
| --- | --- |
| Format support | `float32` (mandatory), `float64` (optional); interleaved and planar; explicit sample rate, channel count, block size |
| Benchmark capability | Processor, chain and engine workload levels; per-block instrumentation; internal multithreading; provided reference data; custom validation |
| Parameter type | boolean, integer, floating-point, enumeration, string |
| Parameter metadata | name, type, units, lower/upper bounds or allowed values, default, human-readable description |
| Unsupported entry | parameter value, format, layout, dimension or feature plus a machine-readable reason and human-readable explanation |

Examples of adapter-specific parameters that the contract must be able to express:
oversampling factor, filter mode, quality setting, internal processing mode, voice
count and processing stages (BR §5.3). These are examples, not mandated v1.0
parameters.

## 5. Audio data contract

**REQ-SDK-06 (Required).** Initial SDK audio requirements are: `float32` mandatory;
`float64` optional; interleaved and planar layouts; explicit sample rate, channel
count and block size; integrations declare unsupported formats and capabilities.
*Source: BR §5.4.*

Derived design:

- A buffer descriptor carries: format enum, layout enum, sample rate (Hz), channel
  count, frame count (block size), pointer(s) to sample data and per-plane strides.
- Input buffers supplied by the host are read-only to the adapter during `Process`;
  output buffers are writable. The adapter must not retain host buffer pointers
  after `Process` returns unless it declares that behavior.
- Block size is the configured value; an adapter that cannot support a requested
  block size must declare it rather than silently reblocking.
- Buffer allocation and signal generation belong to the host and occur outside the
  timed interval (BR §4.2, §7.4).

## 6. Measurements through the SDK

**REQ-SDK-07 (Required).** The contract must support reporting measurements and
must preserve the distinction between measured, estimated, unavailable and
not-applicable values. *Source: BR §5.1, §7.1.*

- Host-driven timing is the default and is the primary source for DSP interval
  timing; the low-overhead aggregate mode is the default (BR §7.3).
- Per-block timing is an opt-in detail level; when enabled the adapter may emit
  one timing sample per `Process` call.
- Adapter-provided counters (for example, internal thread time) are reported as
  **measured** only when genuinely instrumented; derived quantities are reported
  as **estimated**.
- Raw measurements and overhead estimates are separate fields. No subtraction of
  estimated overhead occurs inside the SDK (BR §7.3).
- Metric names, units and status encoding are owned by
  [METRICS_REFERENCE](METRICS_REFERENCE.md) and
  [RESULTS_FORMAT](RESULTS_FORMAT.md).

## 7. Validation through the SDK

**REQ-SDK-08 (Required).** The contract must support validation, including
adapter-provided custom checks and adapter-supplied reference data, and validation
must not distort primary timing. *Source: BR §5.1, §10.1–10.2.*

- Validation is a distinct phase after timing; no validation work may occur inside
  the measured `Process` interval.
- Reference sources the contract must express: adapter-provided data, workload
  reference files, deterministic generation for built-in workloads, and a
  designated reference implementation for direct implementation comparison.
- Tolerances are configurable absolute and relative values (BR §10.2).
- Outcomes are reported per check, not only as a single pass/fail flag.

## 8. Status and error model

Derived design:

- All C ABI functions return an integer status; no C++ exceptions may cross the
  boundary. Detail is returned through output parameters, including a stable
  reason code and a human-readable message.
- Status categories: success, unsupported configuration, incompatible SDK,
  invalid argument, processing failure, validation failure, internal error.
- A processing failure is a **workload failure**; a load or ABI failure is an
  **infrastructure failure**. Classification rules are owned by
  [ARCHITECTURE §6](ARCHITECTURE.md#6-failure-containment).
- Functions must document thread-safety and reentrancy. Instance-level operations
  must not share mutable state with other instances unless declared.

## 9. C++ convenience wrappers

**REQ-SDK-09 (Required).** C++20 convenience wrappers must be provided. *Source: BR §5.1; language confirmed under BR §18 Q46.*

Derived design:

- RAII handles own adapter lifetime and release resources on all exit paths.
- Typed parameter setters map to the C parameter descriptors and validate ranges
  before calling the ABI.
- Exceptions thrown by adapter C++ code are translated to SDK status codes before
  the C boundary.
- Wrappers are header-only or a separate library; they are not part of the stable
  ABI and may evolve independently.

## 10. Static and dynamic packaging

Derived design, satisfying BR §5.1 and BR §6.1:

| Form | Contract |
| --- | --- |
| Static | Adapter exposes a registration entry point invoked during host initialization; it is compiled into the executable. The call stores a read-only descriptor and executes no callback. |
| Dynamic | Shared library exports a versioned entry point plus a read-only metadata descriptor that the host can inspect without executing plugin code. |

Dynamic integrations must expose identity, version, SDK compatibility and declared
capabilities for the trust UI (BR §6.2). Loading, discovery paths, confirmation and
isolation are host responsibilities specified in
[ARCHITECTURE §5](ARCHITECTURE.md#5-plugin-subsystem-host-side).

**Runtime ownership (Derived design).** `ztick_sdk` is a compiled static library whose
implementation language is C++, so a C or Rust FFI host links a C++ runtime even though
the boundary itself is pure C (§1.1). A dynamic adapter must not link `ztick_sdk` or
call its runtime functions: it exposes read-only metadata and the host owns the single
registry. An in-tree CTest check inspects the example module's dynamic symbols to hold
that boundary; P3.8 adds lifecycle callbacks without changing runtime ownership.
Static-library export decoration on Windows is deferred to Phase 7 (post-v1.0, BR §1,
§16).

## 11. Illustrative C shape (non-normative)

The following sketch shows the intended shape only; final signatures are derived
design and may be refined during implementation.

```c
/* Non-normative illustration of the C ABI surface. */
typedef struct ztick_buffer_desc {
    int32_t format;       /* float32 | float64 */
    int32_t layout;       /* interleaved | planar */
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t frames;
    const void * const *planes;
} ztick_buffer_desc;

typedef int32_t (*ztick_process_fn)(void *instance,
                                    const ztick_buffer_desc *in,
                                    ztick_buffer_desc *out);

/* Host calls: query version -> inspect metadata -> create -> describe ->
   configure -> prepare -> process -> finish -> validate -> report -> destroy. */
```

## 12. Third-party conformance checklist

An integration is conformant when it:

1. Reports identity, version, SDK version and capabilities without executing
   processing.
2. Passes SDK major/minor compatibility checks and refuses incompatible hosts.
3. Declares all supported formats, layouts, parameters, ranges, defaults and
   unsupported configurations.
4. Accepts `float32`; declares `float64` support accurately.
5. Keeps generation/validation outside the measured interval and never reblocks
   silently.
6. Reports measurements with the four status labels and retains raw/overhead data
   separately.
7. Provides the required validation hooks and tolerances.
8. Uses only the C ABI across the boundary and never throws exceptions through it.
9. Works in the declared execution mode(s) and records its mode through the host.
10. Can be built and used without modifying ztick-bench itself (BR §17.8).

Test coverage for these items is specified in
[TESTING_STRATEGY](TESTING_STRATEGY.md).
