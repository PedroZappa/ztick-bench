# ztick-bench Makefile
#
# Convenience wrapper around CMake and CTest. CMake is the active build system;
# these targets configure, build, test, analyse and document the existing project.
#
# ztick-bench is an independent project (AGENTS.md, BR §22). Unrelated project
# names, scripts, vendored dependencies and profiling trees are intentionally
# absent. Also not carried over, because
# no target or dependency for them exists in this specification yet:
# scripts/build.sh, per-case Memcheck, and the DSP microbenchmark binary.
#
# Read-only inspection of this file needs no approval; changing it does.

#==============================================================================#
#                                  CONFIG                                      #
#==============================================================================#

# Directories
BUILD_DIR         = build
CI_BUILD_DIR      = build/ci
SANITIZE_DIR      = build/sanitize
DOCS_DIR          = docs
DOCS_BUILD_DIR    = build/docs
DOXYGEN_BUILD_DIR = $(DOCS_BUILD_DIR)/cpp
RUSTDOC_BUILD_DIR = $(DOCS_BUILD_DIR)/rust
RUSTDOC_SOURCE_DIR = $(BUILD_DIR)/cargo/tui/doc
RUSTDOC_INDEX     = $(RUSTDOC_BUILD_DIR)/ztick_tui/index.html
DOCS_INDEX        = $(DOCS_BUILD_DIR)/index.html
DOCS_LANDING      = docs/landing.html
LEGACY_DOCS_DIRS  = $(BUILD_DIR)/doxygen $(BUILD_DIR)/cargo/tui/doc
FORMAT_DIRS       = include src tests tools examples
CPPCHECK_DIRS     = include src tools
TEST_BINARY       = $(BUILD_DIR)/bin/ztick_tests
CLI_BINARY        = $(BUILD_DIR)/bin/ztick-bench
SYSINFO_BINARY    = $(BUILD_DIR)/bin/ztick-sysinfo
VALGRIND_LOG      = $(BUILD_DIR)/memcheck.log

# Tools (override on the command line, e.g. make CMAKE=cmake3)
CMAKE             ?= cmake
CTEST             ?= ctest
VALGRIND          ?= valgrind
DOXYFILE          = Doxyfile

# The formatter is pinned to major version 18 by AGENTS.md. Normally that is the
# native `clang-format-18` binary (as installed in CI). Hosts without one can use
# the local fallback provisioned by `make clang-format-18`; it is selected only
# when neither the command line nor the environment supplied CLANG_FORMAT.
# Resolution is deferred to use time so a fallback provisioned by a prerequisite
# is picked up in the same make invocation.
CLANG_FORMAT_ROOT ?= $(HOME)/.cache/ztick-bench/clang-format-18
_CF18_BIN          = $(CLANG_FORMAT_ROOT)/usr/bin/clang-format-18
_CF18_LIB          = $(CLANG_FORMAT_ROOT)/usr/lib/x86_64-linux-gnu
CLANG_FORMAT      ?= $(shell if command -v clang-format-18 >/dev/null 2>&1; then \
			printf 'clang-format-18'; \
		elif [ -x '$(_CF18_BIN)' ]; then \
			printf '%s' '$(_CF18_BIN)'; \
		else \
			printf 'clang-format-18'; \
		fi)
export CLANG_FORMAT

# The extracted binary needs its bundled LLVM libraries ahead of host ones. The
# original value is captured first so the exported variable cannot refer to
# itself; the path is prepended only when the local formatter was selected.
_ZTICK_LD_LIBRARY_PATH := $(LD_LIBRARY_PATH)
export LD_LIBRARY_PATH = $(if $(filter $(_CF18_BIN),$(CLANG_FORMAT)),$(_CF18_LIB)$(if $(_ZTICK_LD_LIBRARY_PATH),:$(_ZTICK_LD_LIBRARY_PATH)),$(_ZTICK_LD_LIBRARY_PATH))

# Build options
BUILD_TYPE        ?= Release
CMAKE_ARGS        ?=
CI_CMAKE_ARGS     ?= -DZTICK_WERROR=ON
HOST_OS           := $(shell uname -s)

# TUI paths and launch settings. The Rust binary is produced by CMake in the
# normal build tree; the demo rules use isolated XDG directories so plugin
# discovery and trust state never touch the developer's real configuration.
TUI_MANIFEST       = src/tui/Cargo.toml
TUI_BINARY         = $(BUILD_DIR)/cargo/tui/debug/ztick-tui
TUI_CARGO          ?= cargo
TUI_CMAKE_ARGS     = -DZTICK_BUILD_TUI=ON
TUI_DEMO_CMAKE_ARGS = $(TUI_CMAKE_ARGS) -DZTICK_BUILD_EXAMPLES=ON
TUI_PLUGIN_EXT     = $(if $(filter Darwin,$(HOST_OS)),dylib,so)
TUI_DEMO_ROOT      = $(BUILD_DIR)/tui-demo
TUI_DEMO_DATA      = $(abspath $(TUI_DEMO_ROOT)/data)
TUI_DEMO_CONFIG    = $(abspath $(TUI_DEMO_ROOT)/config)
TUI_DEMO_PLUGINS   = $(TUI_DEMO_DATA)/ztick-bench/plugins
TUI_DEMO_MODULE    = $(TUI_DEMO_PLUGINS)/ztick_test_app_dynamic.$(TUI_PLUGIN_EXT)
TUI_COVERAGE_DIR   = $(BUILD_DIR)/coverage/rust
TUI_COVERAGE_MIN_LINES ?=
ZTICK_REQUIRE_RUST_TOOLS ?= 0

# Colors. tput is silenced when TERM is unset (for example in CI), leaving the
# escape sequences empty without polluting every make invocation with errors.
B   = $(shell tput bold 2>/dev/null)
RED = $(shell tput setaf 1 2>/dev/null)
GRN = $(shell tput setaf 2 2>/dev/null)
YEL = $(shell tput setaf 3 2>/dev/null)
BLU = $(shell tput setaf 4 2>/dev/null)
MAG = $(shell tput setaf 5 2>/dev/null)
CYA = $(shell tput setaf 6 2>/dev/null)
D   = $(shell tput sgr0 2>/dev/null)

# Message vars
_SUCCESS = [$(GRN)SUCCESS$(D)]
_INFO    = [$(BLU)INFO$(D)]

# Suppression policy: suppress ids globally only with a stated reason, and only
# while a matching finding exists. Cppcheck reports stale suppressions as
# unmatchedSuppression and exits non-zero with --error-exitcode, so remove them
# once they no longer match. Re-add with the finding that justifies them.
# - checkersReport: informational summary, not a finding.
# - missingIncludeSystem: standard/system headers are intentionally not supplied.
CPPCHECK_FLAGS = --std=c++20 --enable=all --check-level=exhaustive \
                 --error-exitcode=1 --inline-suppr -I include \
                 -I src/core/include -I src/results/include -I src/platform/include -I src/plugin/include \
                 -I src/sdk/include -I src/cli/include -I src/frontend/include \
                 -I tools/ztick-sysinfo/include \
                 --suppress=checkersReport --suppress=missingIncludeSystem

# Valgrind Memcheck: definite/indirect/possible leaks are hard errors;
# still-reachable third-party statics (Catch2) are logged, not fatal.
# Exit 99 distinguishes a Memcheck finding from a failing assertion (1).
VALGRIND_FLAGS = --leak-check=full \
                 --show-leak-kinds=definite,indirect,possible \
                 --errors-for-leak-kinds=definite,indirect,possible \
                 --track-origins=yes \
                 --trace-children=no \
                 --error-exitcode=99

# Sanitizers: CMake's ZTICK_ENABLE_SANITIZERS option is the single source of the
# ASan/UBSan flags; this target only selects the separate build tree and runs the
# full CTest suite under it. LeakSanitizer is unsupported on Darwin and aborts at
# startup, so it is enabled only on other hosts.
ifeq ($(HOST_OS),Darwin)
SANITIZE_ASAN_OPTIONS = detect_stack_use_after_return=1:halt_on_error=1
else
SANITIZE_ASAN_OPTIONS = detect_leaks=1:detect_stack_use_after_return=1:halt_on_error=1
endif

.PHONY: all build test test-verbose clean cppcheck memcheck sanitize run list sysinfo \
	tui tui-build tui-demo tui-check tui-coverage tui-deny tui-demo-reset \
	verify format-check format clang-format-18 sdk-consumer-check \
	docs docs-doxygen docs-rust docs-landing docs-open clean-docs help

#==============================================================================#
#                                  RULES                                       #
#==============================================================================#

##@ Compilation Rules 🏗

all: build	## Build the project (default)

build:	## Configure and build with CMake
	@echo "$(YEL)Configuring $(BUILD_TYPE) build...$(D)"
	@$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) $(CMAKE_ARGS)
	@echo "$(YEL)Building...$(D)"
	@$(CMAKE) --build $(BUILD_DIR) --parallel
	@echo "$(YEL)* $(_SUCCESS) Build completed$(D)"

test: build	## Build and run all tests through CTest
	@echo "$(YEL)Running tests...$(D)"
	@$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure
	@echo "$(YEL)* $(_SUCCESS) Tests completed$(D)"

test-verbose: build	## Verbose tests (FILTER='regex' restricts CTest names)
	@echo "$(YEL)Running tests verbosely$(if $(FILTER), matching $(FILTER),)...$(D)"
	@$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure --verbose \
		$(if $(FILTER),-R "$(FILTER)",)
	@echo "$(YEL)* $(_SUCCESS) Tests completed$(D)"

run: build	## Run a config: make run CONFIG=path/to/config.json
	@if [ -z "$(CONFIG)" ]; then \
		echo "$(RED)Set CONFIG=path/to/config.json$(D)"; \
		exit 1; \
	fi
	@if [ ! -x "$(CLI_BINARY)" ]; then \
		echo "$(RED)$(CLI_BINARY) is not built$(D)"; \
		exit 1; \
	fi
	@echo "$(YEL)Running $(CONFIG)...$(D)"
	@$(CLI_BINARY) run $(CONFIG)
	@echo "$(YEL)* $(_SUCCESS) Benchmark completed$(D)"

list: build	## List built-in benchmark profiles
	@$(CLI_BINARY) list

sysinfo: build	## Print the system-info inventory with availability states
	@echo "$(YEL)Collecting system information...$(D)"
	@$(SYSINFO_BINARY)
	@echo "$(YEL)* $(_SUCCESS) System information printed$(D)"

tui-build:	## Configure with the TUI enabled and build its binary
	@echo "$(YEL)Configuring TUI build...$(D)"
	@$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		$(CMAKE_ARGS) $(TUI_CMAKE_ARGS)
	@$(CMAKE) --build $(BUILD_DIR) --target ztick_tui_binary --parallel
	@echo "$(YEL)* $(_SUCCESS) TUI binary ready at $(TUI_BINARY)$(D)"

tui: tui-build	## Open the TUI (0-9/c/e screens, arrows edit, Ctrl-R run, Ctrl-E execute/export, Ctrl-B/Ctrl-I bundle export/import, Ctrl-S save, Ctrl-O load, p priority, r refresh, q quit)
	@if [ ! -x "$(TUI_BINARY)" ]; then \
		echo "$(RED)$(TUI_BINARY) is not built$(D)"; \
		exit 1; \
	fi
	@echo "$(YEL)Opening TUI... press q to quit$(D)"
	@TERM="$${TERM:-xterm-256color}" "$(TUI_BINARY)"

tui-demo:	## Open the TUI with a seeded plugin and isolated XDG state
	@echo "$(YEL)Configuring isolated TUI demo build...$(D)"
	@$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		$(CMAKE_ARGS) $(TUI_DEMO_CMAKE_ARGS)
	@$(CMAKE) --build $(BUILD_DIR) --target ztick_tui_binary ztick_test_app_dynamic --parallel
	@mkdir -p "$(TUI_DEMO_PLUGINS)"
	@cp -f "$(BUILD_DIR)/lib/ztick_test_app_dynamic.$(TUI_PLUGIN_EXT)" "$(TUI_DEMO_MODULE)"
	@echo "$(YEL)Opening TUI demo... press 3 for Integrations, q to quit$(D)"
	@XDG_DATA_HOME="$(TUI_DEMO_DATA)" XDG_CONFIG_HOME="$(TUI_DEMO_CONFIG)" \
		TERM="$${TERM:-xterm-256color}" "$(TUI_BINARY)"

tui-check:	## Run registered TUI Rust gates through CTest (example adapter enabled)
	@echo "$(YEL)Configuring TUI test build...$(D)"
	@$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		$(CMAKE_ARGS) $(TUI_DEMO_CMAKE_ARGS)
	@$(CMAKE) --build $(BUILD_DIR) --target ztick_tui_binary ztick_test_app_dynamic --parallel
	@echo "$(YEL)Running TUI Rust gates...$(D)"
	@$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure -R '^tui\.'
	@echo "$(YEL)* $(_SUCCESS) TUI Rust gates passed$(D)"

tui-coverage:	## Generate Rust TUI coverage with cargo-llvm-cov (TUI_COVERAGE_MIN_LINES sets an optional floor)
	@echo "$(YEL)Configuring TUI coverage build...$(D)"
	@$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		$(CMAKE_ARGS) $(TUI_DEMO_CMAKE_ARGS) -DZTICK_RUST_COVERAGE=ON \
		-DZTICK_RUST_COVERAGE_MIN_LINES="$(TUI_COVERAGE_MIN_LINES)"
	@$(CMAKE) --build $(BUILD_DIR) --target ztick_frontend ztick_test_app_dynamic --parallel
	@echo "$(YEL)Running Rust TUI coverage gate...$(D)"
	@$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure -R '^tui\.rust\.coverage$$'
	@echo "$(YEL)* $(_SUCCESS) Rust coverage report: $(TUI_COVERAGE_DIR)/html/index.html$(D)"

tui-deny:	## Check Rust dependency licenses, advisories, bans and sources with cargo-deny
	@echo "$(YEL)Configuring Rust dependency-policy gate...$(D)"
	@$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		$(CMAKE_ARGS) $(TUI_CMAKE_ARGS) -DZTICK_RUST_DENY=ON
	@echo "$(YEL)Running Rust dependency-policy gate...$(D)"
	@$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure -R '^tui\.rust\.deny$$'
	@echo "$(YEL)* $(_SUCCESS) Rust dependency policy passed$(D)"

tui-demo-reset:	## Remove isolated TUI demo plugin and trust state
	@echo "$(YEL)Removing TUI demo state...$(D)"
	@rm -rf "$(TUI_DEMO_ROOT)"
	@echo "$(YEL)* $(_SUCCESS) TUI demo state removed$(D)"

clean:	## Remove build trees
	@echo "$(YEL)Removing build trees...$(D)"
	@rm -rf $(BUILD_DIR)
	@echo "$(YEL)* $(_SUCCESS) Build trees cleaned$(D)"

##@ Static Analysis 🔍

cppcheck:	## Run Cppcheck over public headers and sources
	@echo "$(YEL)Running Cppcheck...$(D)"
	@cppcheck $(CPPCHECK_FLAGS) $(CPPCHECK_DIRS) || { \
		echo "$(RED)Cppcheck findings! Fix them or add a documented, targeted suppression.$(D)"; \
		exit 1; \
	}
	@echo "$(YEL)* $(_SUCCESS) Cppcheck found no issues$(D)"

##@ Dynamic Analysis 🧪

memcheck: build	## Run the test binary under Valgrind (FILTER passes Catch2 args)
	@echo "$(YEL)Running Valgrind Memcheck...$(D)"
	@$(VALGRIND) $(VALGRIND_FLAGS) --log-file=$(VALGRIND_LOG) $(TEST_BINARY) $(FILTER)
	@echo "$(YEL)* $(_SUCCESS) Memcheck clean (report: $(VALGRIND_LOG))$(D)"

sanitize:	## Run tests under ASan + UBSan (LSan on non-Darwin hosts)
	@echo "$(YEL)Configuring sanitizer build ($(SANITIZE_DIR))...$(D)"
	@$(CMAKE) -S . -B $(SANITIZE_DIR) -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
		-DZTICK_ENABLE_SANITIZERS=ON $(CMAKE_ARGS)
	@$(CMAKE) --build $(SANITIZE_DIR) --parallel
	@echo "$(YEL)Running sanitized tests...$(D)"
	@ASAN_OPTIONS="$(SANITIZE_ASAN_OPTIONS)" \
		UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
		$(CTEST) --test-dir $(SANITIZE_DIR) --output-on-failure || { \
		echo "$(RED)Sanitizer findings above! Fix before shipping.$(D)"; \
		exit 1; \
	}
	@echo "$(YEL)* $(_SUCCESS) Sanitizers: no findings$(D)"

##@ Verification ✅

verify: clang-format-18	## Run the full CI gate (CI_CMAKE_ARGS defaults to -DZTICK_WERROR=ON; ZTICK_REQUIRE_RUST_TOOLS=1 requires coverage/deny tools)
	@CI_BUILD_DIR="$(CI_BUILD_DIR)" CI_CMAKE_ARGS="$(CI_CMAKE_ARGS)" \
		CMAKE="$(CMAKE)" CTEST="$(CTEST)" TUI_CARGO="$(TUI_CARGO)" \
		CMAKE_BUILD_TYPE="$(BUILD_TYPE)" \
		ZTICK_REQUIRE_RUST_TOOLS="$(ZTICK_REQUIRE_RUST_TOOLS)" \
		./scripts/verify.sh

##@ Utility Rules 🛠

clang-format-18:	## Provision local clang-format 18 if no native pinned binary exists
	@./scripts/setup_clang_format.sh "$(CLANG_FORMAT_ROOT)"

format-check: clang-format-18	## Verify C/C++ files follow .clang-format with the pinned formatter (fails if not)
	@echo "$(YEL)Checking formatting with $(CLANG_FORMAT)...$(D)"
	@find $(FORMAT_DIRS) -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' \) \
		-print0 2>/dev/null | xargs -0 -r $(CLANG_FORMAT) --dry-run --Werror || { \
		echo "$(RED)Formatting errors found! Run 'make format'.$(D)"; \
		exit 1; \
	}
	@echo "$(YEL)* $(_SUCCESS) All files are correctly formatted$(D)"

format: clang-format-18	## Format C/C++ files with the pinned formatter
	@echo "$(YEL)Formatting C/C++ files with $(CLANG_FORMAT)...$(D)"
	@find $(FORMAT_DIRS) -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' \) \
		-print0 2>/dev/null | xargs -0 -r $(CLANG_FORMAT) -i
	@echo "$(YEL)* $(_SUCCESS) Codebase formatted$(D)"

sdk-consumer-check:	## Verify an external consumer can find_package(ztick-sdk)
	@echo "$(YEL)Checking SDK consumer...$(D)"
	@CMAKE="$(CMAKE)" ./scripts/check_sdk_consumer.sh
	@echo "$(YEL)* $(_SUCCESS) SDK consumer check passed$(D)"

external-apps-check:	## Build and run both integrations out of tree (P3.9)
	@echo "$(YEL)Checking external-style apps...$(D)"
	@CMAKE="$(CMAKE)" CTEST="$(CTEST)" ./scripts/check_external_apps.sh
	@echo "$(YEL)* $(_SUCCESS) External-style apps check passed$(D)"

##@ Documentation Rules 📚

# Internal prerequisites keep `docs` as the single documented entry point.
docs-doxygen:
	@if [ ! -f $(DOXYFILE) ]; then \
		echo "$(RED)Error: $(DOXYFILE) not found.$(D)"; \
		exit 1; \
	fi
	@mkdir -p $(DOCS_BUILD_DIR)
	@doxygen $(DOXYFILE)
	@echo "$(YEL)* $(_SUCCESS) Doxygen documentation generated in $(DOXYGEN_BUILD_DIR)$(D)"

docs-rust:
	@echo "$(YEL)Building Rust documentation...$(D)"
	@$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		$(CMAKE_ARGS) $(TUI_CMAKE_ARGS)
	@$(CMAKE) --build $(BUILD_DIR) --target ztick_frontend --parallel
	@CARGO_TARGET_DIR="$(abspath $(BUILD_DIR)/cargo/tui)" \
		ZTICK_FRONTEND_LIB_DIR="$(abspath $(BUILD_DIR)/lib)" \
		LD_LIBRARY_PATH="$(abspath $(BUILD_DIR)/lib)$${LD_LIBRARY_PATH:+:$$LD_LIBRARY_PATH}" \
		RUSTDOCFLAGS="-D warnings" $(TUI_CARGO) doc --manifest-path $(TUI_MANIFEST) --no-deps
	@mkdir -p $(DOCS_BUILD_DIR)
	@rm -rf "$(RUSTDOC_BUILD_DIR)"
	@cp -R "$(RUSTDOC_SOURCE_DIR)" "$(RUSTDOC_BUILD_DIR)"
	@echo "$(YEL)* $(_SUCCESS) Rust documentation generated in $(RUSTDOC_BUILD_DIR)$(D)"

docs-landing:
	@mkdir -p $(DOCS_BUILD_DIR)
	@cp -f "$(DOCS_LANDING)" "$(DOCS_INDEX)"

docs: docs-doxygen docs-rust docs-landing	## Generate all docs in one shared tree (Doxygen + Rust rustdoc)
	@echo "$(YEL)* $(_SUCCESS) Combined documentation generated in $(DOCS_BUILD_DIR)$(D)"

docs-open:	## Open the combined documentation landing page
	@if [ ! -f "$(DOCS_INDEX)" ] || [ ! -f "$(DOXYGEN_BUILD_DIR)/index.html" ] || \
		[ ! -f "$(RUSTDOC_INDEX)" ]; then \
		echo "$(RED)Documentation not found. Run 'make docs' first.$(D)"; \
		exit 1; \
	fi
	@case "$$(uname)" in \
		Darwin) open "$(DOCS_INDEX)" ;; \
		Linux) xdg-open "$(DOCS_INDEX)" || open "$(DOCS_INDEX)" ;; \
		*) echo "$(RED)Unsupported OS. Open $(DOCS_INDEX) manually.$(D)" ;; \
	esac
	@echo "$(YEL)* $(_SUCCESS) Browser launched$(D)"

clean-docs:	## Remove generated documentation artifacts
	@echo "$(YEL)Removing generated documentation...$(D)"
	@rm -rf "$(DOCS_BUILD_DIR)" $(LEGACY_DOCS_DIRS)
	@echo "$(YEL)* $(_SUCCESS) Documentation cleaned$(D)"

##@ Help 󰛵

help:	## Display this help page
	@awk 'BEGIN {FS = ":.*##"; \
			printf "\n=> Usage:\n\tmake $(GRN)<target>$(D)\n"} \
		/^[a-zA-Z_0-9-]+:.*?##/ { \
			printf "\t$(GRN)%-18s$(D) %s\n", $$1, $$2 } \
		/^##@/ { \
			printf "\n=> %s\n", substr($$0, 5) } ' Makefile
