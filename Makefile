# ztick-bench Makefile
#
# Convenience wrapper around CMake and CTest. CMake remains the build system.
# The CMake project lands in Phase 1 (step P1.1); until then these targets have
# nothing to configure.
#
# Structure adapted from the ALIENMETAL Makefile. ztick-bench is an independent
# project (AGENTS.md, BR §22): ALIENMETAL names, scripts, vendored dependencies
# and profiling trees are intentionally absent. Also not carried over, because
# no target or dependency for them exists in this specification yet:
# scripts/build.sh, per-case Memcheck, and the DSP microbenchmark binary.
#
# Read-only inspection of this file needs no approval; changing it does.

#==============================================================================#
#                                  CONFIG                                      #
#==============================================================================#

# Directories
BUILD_DIR         = build
SANITIZE_DIR      = build-sanitize
DOCS_DIR          = docs
DOXYGEN_BUILD_DIR = build/doxygen
FORMAT_DIRS       = include src tests tools examples
CPPCHECK_DIRS     = include src
TEST_BINARY       = $(BUILD_DIR)/bin/ztick_tests
CLI_BINARY        = $(BUILD_DIR)/bin/ztick-bench
VALGRIND_LOG      = $(BUILD_DIR)/memcheck.log

# Tools (override on the command line, e.g. make CMAKE=cmake3)
CMAKE             ?= cmake
CTEST             ?= ctest
VALGRIND          ?= valgrind
DOXYFILE          = Doxyfile

# Build options
BUILD_TYPE        ?= Release
CMAKE_ARGS        ?=
HOST_OS           := $(shell uname -s)

# Colors
B   = $(shell tput bold)
RED = $(shell tput setaf 1)
GRN = $(shell tput setaf 2)
YEL = $(shell tput setaf 3)
BLU = $(shell tput setaf 4)
MAG = $(shell tput setaf 5)
CYA = $(shell tput setaf 6)
D   = $(shell tput sgr0)

# Message vars
_SUCCESS = [$(GRN)SUCCESS$(D)]
_INFO    = [$(BLU)INFO$(D)]

# Suppression policy: suppress ids globally only with a stated reason, and only
# while a matching finding exists. Cppcheck reports stale suppressions as
# unmatchedSuppression and exits non-zero with --error-exitcode, so remove them
# once they no longer match. Re-add with the finding that justifies them.
# - checkersReport: informational summary, not a finding.
CPPCHECK_FLAGS = --std=c++20 --enable=all --check-level=exhaustive \
                 --error-exitcode=1 --inline-suppr -I include \
                 --suppress=checkersReport

# Valgrind Memcheck: definite/indirect/possible leaks are hard errors;
# still-reachable third-party statics (Catch2) are logged, not fatal.
# Exit 99 distinguishes a Memcheck finding from a failing assertion (1).
VALGRIND_FLAGS = --leak-check=full \
                 --show-leak-kinds=definite,indirect,possible \
                 --errors-for-leak-kinds=definite,indirect,possible \
                 --track-origins=yes \
                 --trace-children=no \
                 --error-exitcode=99

# Sanitizers: ASan + UBSan in a separate tree so normal builds are untouched.
# LeakSanitizer is unsupported on Darwin and aborts at startup, so it is enabled
# only on other hosts. Catch2 is instrumented too (dev-only dependency).
SANITIZE_CXXFLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer \
                    -fsanitize-address-use-after-scope -fno-sanitize-recover=all
ifeq ($(HOST_OS),Darwin)
SANITIZE_ASAN_OPTIONS = detect_stack_use_after_return=1:halt_on_error=1
else
SANITIZE_ASAN_OPTIONS = detect_leaks=1:detect_stack_use_after_return=1:halt_on_error=1
endif

.PHONY: all build test test-verbose clean cppcheck memcheck sanitize run \
	format-check format sdk-consumer-check docs docs-open clean-docs help

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
	@echo "$(YEL)Running $(CONFIG)...$(D)"
	@$(CLI_BINARY) run $(CONFIG)
	@echo "$(YEL)* $(_SUCCESS) Benchmark completed$(D)"

clean:	## Remove build trees
	@echo "$(YEL)Removing build trees...$(D)"
	@rm -rf $(BUILD_DIR) $(SANITIZE_DIR)
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
		-DCMAKE_CXX_FLAGS="$(SANITIZE_CXXFLAGS)" $(CMAKE_ARGS)
	@$(CMAKE) --build $(SANITIZE_DIR) --parallel
	@echo "$(YEL)Running sanitized tests...$(D)"
	@ASAN_OPTIONS="$(SANITIZE_ASAN_OPTIONS)" \
		UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
		$(SANITIZE_DIR)/bin/ztick_tests || { \
		echo "$(RED)Sanitizer findings above! Fix before shipping.$(D)"; \
		exit 1; \
	}
	@echo "$(YEL)* $(_SUCCESS) Sanitizers: no findings$(D)"

##@ Utility Rules 🛠

format-check:	## Verify C/C++ files follow .clang-format (fails if not)
	@echo "$(YEL)Checking formatting...$(D)"
	@find $(FORMAT_DIRS) -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' \) \
		-print0 2>/dev/null | xargs -0 -r clang-format --dry-run --Werror || { \
		echo "$(RED)Formatting errors found! Run 'make format'.$(D)"; \
		exit 1; \
	}
	@echo "$(YEL)* $(_SUCCESS) All files are correctly formatted$(D)"

format:	## Format C/C++ files with clang-format
	@echo "$(YEL)Formatting C/C++ files...$(D)"
	@find $(FORMAT_DIRS) -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' \) \
		-print0 2>/dev/null | xargs -0 -r clang-format -i
	@echo "$(YEL)* $(_SUCCESS) Codebase formatted$(D)"

sdk-consumer-check:	## Verify an external consumer can find_package(ztick-sdk)
	@echo "$(YEL)Checking SDK consumer...$(D)"
	@./scripts/check_sdk_consumer.sh
	@echo "$(YEL)* $(_SUCCESS) SDK consumer check passed$(D)"

##@ Documentation Rules 📚

docs:	## Generate Doxygen documentation (requires Doxyfile)
	@if [ ! -f $(DOXYFILE) ]; then \
		echo "$(RED)Error: $(DOXYFILE) not found.$(D)"; \
		exit 1; \
	fi
	@doxygen $(DOXYFILE)
	@echo "$(YEL)* $(_SUCCESS) Documentation generated in $(DOXYGEN_BUILD_DIR)$(D)"

docs-open:	## Open the generated documentation
	@if [ ! -f $(DOXYGEN_BUILD_DIR)/html/index.html ]; then \
		echo "$(RED)Documentation not found. Run 'make docs' first.$(D)"; \
		exit 1; \
	fi
	@case "$$(uname)" in \
		Darwin) open $(DOXYGEN_BUILD_DIR)/html/index.html ;; \
		Linux) xdg-open $(DOXYGEN_BUILD_DIR)/html/index.html || \
			open $(DOXYGEN_BUILD_DIR)/html/index.html ;; \
		*) echo "$(RED)Unsupported OS. Open the file manually.$(D)" ;; \
	esac
	@echo "$(YEL)* $(_SUCCESS) Browser launched$(D)"

clean-docs:	## Remove generated documentation artifacts
	@echo "$(YEL)Removing generated documentation...$(D)"
	@rm -rf $(DOXYGEN_BUILD_DIR) html latex
	@echo "$(YEL)* $(_SUCCESS) Documentation cleaned$(D)"

##@ Help 󰛵

help:	## Display this help page
	@awk 'BEGIN {FS = ":.*##"; \
			printf "\n=> Usage:\n\tmake $(GRN)<target>$(D)\n"} \
		/^[a-zA-Z_0-9-]+:.*?##/ { \
			printf "\t$(GRN)%-18s$(D) %s\n", $$1, $$2 } \
		/^##@/ { \
			printf "\n=> %s\n", substr($$0, 5) } ' Makefile
