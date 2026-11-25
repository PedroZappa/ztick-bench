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

- **Project identity:** ztick-bench is an independent open-source project. Its own
  development rules are defined in this file (BR §22).
- **Core portability:** the core must stay independent of individual DSP libraries
  and frameworks. Linux-specific behavior lives behind platform abstractions.
- **C ABI at the integration boundary**, with C++ convenience wrappers; do not
  expose a stable C++ ABI as the plugin contract.
- **Dependency separation:** core engine/SDK, TUI, CLI, system monitoring, plugin
  loading, JSON serialization, and testing are separate concerns. Optional features
  must not force dependencies on core/CLI-only users.
- **Prefer maintained, permissively licensed libraries**; document versions and
  licenses when a dependency is added.
- **C++20 and CMake are confirmed** (Q46) for the core, CLI and SDK, with a versioned
  C ABI at the integration boundary and C++ convenience wrappers. The TUI is the
  sole exception: Rust 1.88+ with ratatui 0.30.2, calling the C ABI via FFI
  (BR §21, confirmed 2026-11-22). See
  [TUI §1](docs/TUI_SPECIFICATION.md#1-principles).
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
- **No mandated runtime third-party libraries** for JSON or IPC; BR §15 principles
  govern all dependency choices. ratatui is the one mandated TUI library (BR §21).
  Catch2 v3 is the confirmed **dev-only** C/C++ test framework (BR §22); never link
  it into installed targets.

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
- **Formatting:** `clang-format` 18 with LLVM base, `ColumnLimit: 100`,
  `IndentWidth: 2`, attached braces; run before committing. The version is pinned
  through the Makefile's `CLANG_FORMAT` variable (override only when intentionally
  changing the pin). Rust TUI sources use `rustfmt` and `clippy` instead (Phase 5).
  `.editorconfig` enforces UTF-8, LF endings and trailing-whitespace removal.
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

## Implementation rules (agent behavior)

- **Unit-test every function.** When adding or changing a function, add or update a
  deterministic unit test that asserts its observable behavior, including error and
  edge paths. A change is not complete until its tests pass. Test scope and
  exemptions are owned by
  [TESTING_STRATEGY §2](docs/TESTING_STRATEGY.md#2-test-layers).
- **Verify mathematical formulas independently.** Every implemented formula from
  [METRICS_REFERENCE §3](docs/METRICS_REFERENCE.md#3-definitions-and-formulas)
  must be tested against an independently computed expected value, never against
  the implementation itself.
- **Keep every test registered with CTest** so `make test` runs it; Catch2 remains
  a dev-only dependency (BR §22). Rust `cargo test` results join the same gate when
  the TUI lands (Phase 5).
- **Document all C/C++ code with Doxygen comments.** Every file, namespace, type,
  function, method, enum, constant, macro and member gets a Doxygen comment
  (`/** ... */` or `///`) at its declaration, including internal code. Use
  `@brief`, `@param`, `@return` and `@note`/`@warning` as applicable; state units,
  preconditions, ownership/lifetime, status/error behavior and thread-safety where
  relevant. C ABI headers document the C11 surface and opaque handles; C++ wrappers
  name their C ABI counterpart. Comments are updated with the code they describe.
- **Document all Rust code with rustdoc comments.** Every Rust file, module, type,
  function, method, enum, constant, macro and member gets a `///` or `//!` comment
  at its declaration, including internal and test code. Use `# Safety`,
  `# Parameters`, `# Returns` and `# Errors`/`# Panics` sections as applicable;
  state units, preconditions, ownership/lifetime, status/error behavior and
  thread-safety where relevant. Comments are updated with the code they describe.
- **Keep generated documentation buildable and uncommitted.** `make docs` is the
  single entry point and generates a shared `build/docs` tree: Doxygen under
  `cpp/`, Rust rustdoc under `rust/ztick_tui/`, and an `index.html` landing page;
  `make docs-open` opens that landing page and `make clean-docs` removes the tree.
  Resolve Doxygen and rustdoc warnings for changed code before finishing a change,
  and never commit generated output.

## Git operations (agent behavior)

- **Read-only inspection is allowed.** Commands such as `git status`, `git diff`,
  `git log`, `git show`, `git branch`, `git remote -v` and `git ls-remote` may be
  run freely; they do not change the repository.
- **Never commit on your own.** The agent must not run `git commit` unless the
  user explicitly requests that specific commit at that moment.
- **Ask before any repo-modifying git command.** This includes `git add`,
  `git commit`, `git stash`, `git switch` / `git checkout`, `git merge`,
  `git rebase`, `git reset`, `git push`, `git pull`, `git tag` and `git clean`.
  State the exact command and its purpose, then wait for approval before running it.
- **Approval is per command, not blanket.** Prior approval for one git operation
  does not authorize another. When the user asks for a git operation, run only
  that operation and report the result; do not chain extra git actions.
- **The commit and branch conventions above** apply only when the user explicitly
  asks for a commit or branch. They are never a reason to initiate one.

## Reusable command flows (Makefile)

- **Promote useful command flows into `make` rules.** When a command sequence is
  worth running again or handing to another contributor, add a target to the root
  `Makefile` instead of leaving it as an ad-hoc shell command in a document, chat
  or memory. The `Makefile` is the canonical home for developer workflows; CMake
  remains the build system.
- **Keep targets thin.** Wrap `cmake`, `ctest` and the project binaries rather than
  duplicating build logic. Expose anything a developer may change as an
  overridable variable (`CMAKE`, `CTEST`, `BUILD_TYPE`, `FILTER`, `CONFIG`, ...).
- **Follow the existing target conventions:** hyphenated target names, a `##`
  description so `make help` picks it up, placement under the matching `##@`
  banner, and a `.PHONY` entry for non-file targets.
- **Keep `make help` and dry runs side-effect free.** New targets must not create
  files, configure builds or modify the repository just to display help.
- **Document arguments in the help text**, not in a separate list, so `make help`
  stays authoritative. Once a target exists, prefer it over the raw command;
  keep the raw form only where it explains what the target does.
- **Extract cumbersome rules into `scripts/`.** When a target grows beyond a short
  command or two — multi-step logic, loops, conditionals or portability shims —
  move the body into an executable `scripts/<name>.sh` and leave a thin `make` rule
  that calls it with the target's arguments. Scripts use `snake_case` names, start
  with a shebang and `# SPDX-License-Identifier: MIT`, fail fast (`set -euo
  pipefail` where supported), and stay portable across the platforms in scope.
  The `make` rule keeps its `##` help text and remains the only entry point.

## Planned commands (CLI subcommands land in Phase 4)

The build/test steps below are already wrapped by `make build` and `make test`;
the `ztick-bench` subcommands become available with roadmap Phase 4. See
Reusable command flows above.

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
- [ ] Reusable command flows are exposed as `make` targets with `##` help text.
- [ ] Cumbersome rule logic lives in `scripts/` behind a thin `make` wrapper.
