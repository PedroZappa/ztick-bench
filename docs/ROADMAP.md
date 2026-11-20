# Roadmap

| | |
| --- | --- |
| **Status** | Technical specification — v1.0 baseline complete |
| **Source of truth** | [DECISIONS.md](DECISIONS.md) BR §1, §2.3, §12.3, §16, §21 |
| **Authoritative for** | Phase ordering, sequencing rationale, deferred work, decision dependencies |
| **Not authoritative for** | v1.0 scope detail ([PROJECT_SPEC](PROJECT_SPEC.md)), test cases ([TESTING_STRATEGY](TESTING_STRATEGY.md)) |

---

## 1. Accepted roadmap

The phase order below is the accepted roadmap from BR §16. Deliverables and exit
criteria are **planning guidance only**, not binding acceptance gates; only the
BR §16 phase order and BR §17 acceptance criteria bind. The roadmap is
dependency-ordered: no dates or estimates are included (BR §21).

| # | Phase | Deliverables (derived) | Exit criterion (derived) |
| --- | --- | --- | --- |
| 1 | Foundation | Repository, CMake, SDK headers and core types, system information abstraction, `ztick-sysinfo` dev tool | Core and platform abstraction build; `ztick-sysinfo` prints system info with availability states |
| 2 | Benchmark engine | Workloads, input generators, execution, timing, statistics, validation | Generators and timing levels work; metrics computed per [METRICS](METRICS_REFERENCE.md) |
| 3 | Integrations | Static and dynamic adapters, two custom SDK test applications | Both test apps run end-to-end ([TESTING_STRATEGY §3](TESTING_STRATEGY.md#3-custom-sdk-test-applications)) |
| 4 | CLI and results | Profiles, JSON/CSV, comparison, regression detection, CI support | CLI workflows, portable config, comparisons and CI exit codes verified |
| 5 | TUI | Dashboard, configuration, execution monitoring, result inspection | Principal TUI workflows verified |
| 6 | Reliability | Stress tests, isolation, overhead validation, repeatability | Overhead, repeatability, failure and isolation tests pass |
| 7 | Platform expansion | macOS support, subsequent Windows preparation | Platform abstraction extended; unsupported capabilities reported |

**REQ-ROAD-01 (Required).** Validate the SDK and core engine through working custom
applications before investing heavily in the TUI. *Source: BR §16.*

**REQ-ROAD-02 (Confirmed).** v1.0 comprises phases 1–6; phase 7 (macOS support and
Windows preparation) is post-v1.0. *Source: BR §21.*

**REQ-ROAD-03 (Confirmed).** The deliverables and exit criteria in §1 are planning
guidance, not binding acceptance gates; only BR §16 ordering and BR §17 criteria
are binding. *Source: BR §21.*

**REQ-ROAD-04 (Confirmed).** The roadmap is dependency-ordered only; no dates or
estimates are added. *Source: BR §21.*

This is why Phase 3 precedes Phase 5: a dashboard cannot meaningfully validate an
unproven adapter contract.

## 2. Mode rollout

BR §2.3 defines the order:

1. **Offline DSP benchmarks** — first implementation priority (Phases 2–6).
2. **Simulated real-time benchmarks** — later.
3. **Live audio benchmarks** — later.

The architecture must accommodate later modes without putting live-audio or
platform-specific assumptions into the offline core (BR §2.3,
REQ-ARCH-03).

## 3. Deferred and future work

Recorded separately from v1.0 requirements per BR §19.

| Item | Source | Trigger / notes |
| --- | --- | --- |
| Simulated real-time mode | BR §2.3, §16 | Requires deadline model and deadline-miss metric activation |
| Live audio mode | BR §2.3, §16 | Requires audio backend/device integration behind platform abstraction |
| White/pink noise, sweeps, silence, musical material, custom audio files | BR §4.2 | Randomized generators need configurable seeds (BR §4.3) |
| REAPER, Ardour, Bitwig Studio, Pure Data, SuperCollider integrations | BR §5.2 | Future possibilities, not first-release dependencies |
| Plugin marketplace / online package manager | BR §6.1 | Explicitly not required for v1.0 |
| SQLite historical storage/querying | BR §12.3 | Later local storage layer |
| macOS support | BR §1, §16 | Phase 7 |
| Windows preparation | BR §1, §16 | Phase 7 and beyond |
| Benchmark suites (`suite run`) | BR §18 Q47, §21 | **Deferred 2026-11-20** — post-v1.0; config format kept suite-friendly |

## 4. Decision dependencies

| Decision | Affects | Status |
| --- | --- | --- |
| Q46 — language standard | Phase 1 build/toolchain, SDK wrappers, all C++ code | **Confirmed 2026-11-20:** C++20 + CMake + versioned C ABI |
| Q47 — benchmark suites | Phase 4 CLI/config scope, acceptance criteria | **Deferred 2026-11-20:** not in v1.0; config kept suite-friendly |
| Q48 — documentation layout | This document set and ownership map | **Confirmed 2026-11-20:** multi-file layout adopted |

See [PROJECT_SPEC §7](PROJECT_SPEC.md#7-decision-register) for the full register.
All decisions are resolved; any future change to a confirmed decision requires
explicit approval (BR §19).

## 5. v1.0 exit criteria

v1.0 ends after phase 6 (REQ-ROAD-02). Completion is defined by BR §17 and traced
to tests in
[TESTING_STRATEGY §10](TESTING_STRATEGY.md#10-acceptance-criteria-traceability).
Platform-dependent capabilities must be reported as unsupported or unavailable
rather than silently omitted (BR §17).
