# AGENTS.md — concise development rules

This file contains rules and commands only. It is **not** the project specification.

## Sources of truth

1. [docs/DECISIONS.md](docs/DECISIONS.md) — the **baseline record** (BR),
   including §21 post-baseline confirmations and §22 pre-implementation decisions.
   Q46 and Q48 are confirmed; Q47 is deferred to post-v1.0. No decision remains open.
2. [docs/PROJECT_SPEC.md](docs/PROJECT_SPEC.md) — scope, document ownership,
   decision register.
3. The document that **owns** a topic (see ownership map below) — authoritative
   technical detail for that topic.

## Ownership map — do not duplicate detail

| Topic | Edit this file | Link from others |
| --- | --- | --- |
| Scope, v1.0 boundary, decision register | `docs/PROJECT_SPEC.md` | all |
| Components, dependency direction, build targets, host plugin/trust runtime | `docs/ARCHITECTURE.md` | SDK, TUI, TESTING |
| C ABI, C++ wrappers, adapter contract, version checks | `docs/SDK_SPECIFICATION.md` | ARCH, TESTING |
| Workloads, generators, profiles, execution lifecycle, validation behavior | `docs/BENCHMARK_SPECIFICATION.md` | METRICS, RESULTS |
| Metric definitions, formulas, statuses, regression statistics | `docs/METRICS_REFERENCE.md` | BENCHMARK, RESULTS |
| TUI screens and interaction | `docs/TUI_SPECIFICATION.md` | PROJECT_SPEC |
| System info, monitoring, affinity, scheduling, priority | `docs/PLATFORM_MONITORING.md` | BENCHMARK, ARCH |
| JSON/CSV schemas, config format, metadata, portability, failure encoding | `docs/RESULTS_FORMAT.md` | BENCHMARK, TUI, TESTING |
| Test strategy and acceptance traceability | `docs/TESTING_STRATEGY.md` | ROADMAP |
| Phase ordering and deferred work | `docs/ROADMAP.md` | README, PROJECT_SPEC |
| Terminology | `docs/GLOSSARY.md` | all |

If a topic belongs to another file, add a link instead of re-documenting it.

## Hard constraints

- **Project identity:** ztick-bench is not ALIENMETAL and does not inherit its
  development standards. Its own rules are defined in this file (BR §22).
- **Core portability:** the core must stay independent of individual DSP libraries
  and frameworks. Linux-specific behavior lives behind platform abstractions.
- **C ABI at the integration boundary**, with C++ convenience wrappers; do not
  expose a stable C++ ABI as the plugin contract.
- **Dependency separation:** core engine/SDK, TUI, CLI, system monitoring, plugin
  loading, JSON serialization, and testing are separate concerns. Optional features
  must not force dependencies on core/CLI-only users.
- **Prefer maintained, permissively licensed libraries**; document versions and
  licenses when a dependency is added.
- **C++20 and CMake are confirmed** (Q46), with a versioned C ABI at the integration
  boundary and C++ convenience wrappers.
- **Measurement integrity:** input generation and setup stay outside the primary
  timed DSP interval unless end-to-end timing is explicitly selected. Raw
  measurements and overhead estimates are stored independently; never silently
  subtract overhead. Validation never distorts timing.
- **Unavailable ≠ omitted:** platform-dependent capabilities must be reported as
  `unsupported` or `unavailable`, never silently dropped.
- **Result integrity:** preserve completed measurements, mark incomplete runs as
  incomplete, and write result files atomically.
- **v1.0 is roadmap phases 1–6**; phase 7 (macOS/Windows) is post-v1.0. Phase
  detail is planning guidance, not a binding gate; only BR §16 order and BR §17
  acceptance criteria bind.
- **No mandated runtime third-party libraries** for JSON, TUI or IPC; BR §15
  principles govern all dependency choices. Catch2 v3 is the confirmed **dev-only**
  test framework (BR §22); never link it into installed targets.

## Naming, formatting and workflow rules

- **Files/directories:** `snake_case`. The repository layout is defined in
  [ARCHITECTURE §3.1](docs/ARCHITECTURE.md#31-source-tree-layout-derived-design);
  public headers live under `include/ztick/`, private headers beside their sources
  or under `src/<area>/include/`.
- **C++:** namespace `ztick` for core code and `ztick::sdk` for wrappers; types
  `PascalCase`; functions and variables `snake_case`; constants `kPascalCase`;
  macros `ZTICK_UPPER_SNAKE`.
- **C ABI:** every exported symbol prefixed `ztick_`, every macro `ZTICK_`.
  Public headers use fixed-width C types only; no C++ types, exceptions or C++ ABI.
- **Formatting:** `clang-format` with LLVM base, `ColumnLimit: 100`,
  `IndentWidth: 2`, attached braces; run before committing. `.editorconfig`
  enforces UTF-8, LF endings and trailing-whitespace removal.
- **Commits:** Conventional Commits (`feat:`, `fix:`, `docs:`, `test:`, `build:`,
  `refactor:`, `chore:`), imperative subject of at most 72 characters, one logical
  change per commit.
- **Branches:** `main` is the integration branch; short-lived `feature/<topic>`,
  `fix/<topic>` and `docs/<topic>` branches merged by reviewed pull request,
  squash preferred.
- **Licensing:** MIT; new source files start with `// SPDX-License-Identifier: MIT`.

## Change rules

- Treat the ledger's confirmed decisions as binding. If an implementation decision
  requires changing one, record the reason and obtain explicit approval first.
- Do not promote a future idea or an open question into a v1.0 requirement.
- When terminology is introduced, add it to `docs/GLOSSARY.md` and use it
  consistently everywhere.
- Keep requirement references in the form `BR §n` (Baseline Record =
  `docs/DECISIONS.md`) so the docs remain traceable to the decisions.

## Planned commands (Foundation phase not yet implemented)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ztick-bench system
ztick-bench list
ztick-bench run <config.json>
ztick-bench compare <a.json> <b.json>
ztick-bench report <run.json>
ztick-bench plugin list
ztick-bench plugin inspect <path>
```

## Review checklist before finishing a doc change

- [ ] Every normative statement traces to `BR §n` or is clearly marked as derived
      design.
- [ ] Confirmed and deferred decisions are reflected consistently; nothing is left
      marked open.
- [ ] Cross-topic content is linked, not copied.
- [ ] v1.0 requirements and deferred work remain visually distinct.
- [ ] Terminology matches the glossary.
