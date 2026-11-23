// SPDX-License-Identifier: MIT
/**
 * @file app_b_harness.hpp
 * @brief Observable results of the dynamic App B end-to-end harness.
 *
 * The harness is the host side of custom test app B. It inspects a shared library
 * without executing it, negotiates SDK version and capabilities, applies explicit
 * first-run trust policy, selects an execution runner, and drives the complete C ABI
 * lifecycle for the engine workload. Internal threading, Strict validation and
 * instrumented measurements are exercised in both in-process and out-of-process
 * modes (BR §6, §17.2, TESTING_STRATEGY §3).
 *
 * @note The out-of-process protocol carries only a final status, so the host result
 *       in that mode records the run and its outcome while the child retains the
 *       detailed metric and validation evidence (ARCHITECTURE §4).
 */

#ifndef ZTICK_APP_B_HARNESS_HPP
#define ZTICK_APP_B_HARNESS_HPP

#include <ztick/sdk.h>

#include <cstdint>
#include <string>
#include <vector>

/** @brief Dynamic engine SDK test application. */
namespace ztick::example::app_b {

/** @brief Isolation mode requested from the App B harness. */
enum class HarnessIsolation : std::uint8_t {
  /** @brief Execute the dynamic adapter on the harness thread. */
  in_process = 0,
  /** @brief Execute the dynamic adapter in an isolated child process. */
  out_of_process = 1,
};

/** @brief Inputs supplied to the App B harness by a front end or executable. */
struct HarnessOptions {
  /** @brief Path to the dynamic App B shared library; must be non-empty. */
  std::string module_path;
  /** @brief Path to this harness executable; required for out-of-process mode. */
  std::string child_executable;
  /** @brief Requested isolation mode. */
  HarnessIsolation isolation{HarnessIsolation::in_process};
};

/** @brief One metric emitted through the SDK metric sink. */
struct MetricReport {
  /** @brief Stable machine-readable metric name. */
  std::string name;
  /** @brief Reported numeric value. */
  double value{0.0};
  /** @brief Reported unit, empty when dimensionless. */
  std::string unit;
  /** @brief Provenance label attached by the adapter. */
  ztick_metric_status status{ztick_metric_status_unavailable};
  /** @brief Collection method or instrument source. */
  std::string source;
};

/** @brief One per-check Strict validation outcome reported by the adapter. */
struct ValidationReportEntry {
  /** @brief Stable check identifier. */
  std::string check_name;
  /** @brief Completed check outcome. */
  ztick_status status{ztick_status_internal_error};
  /** @brief Human-readable detail for the outcome. */
  std::string details;
  /** @brief Recorded reference source. */
  std::string reference_source;
};

/** @brief Complete observable result of one App B harness run. */
struct HarnessResult {
  /** @brief True only when every harness expectation passed. */
  bool ok{false};
  /** @brief First failed expectation, or an empty string on success. */
  std::string error;
  /** @brief `ztick_status` code returned by metadata inspection. */
  std::int32_t inspection_status{ztick_status_internal_error};
  /** @brief Status returned by the SDK version query. */
  std::int32_t sdk_version_status{ztick_status_internal_error};
  /** @brief `ztick_status` code returned by negotiation. */
  std::int32_t negotiation_status{ztick_status_internal_error};
  /** @brief `ztick_status` code returned by runtime module loading. */
  std::int32_t runtime_load_status{ztick_status_internal_error};
  /** @brief Numeric trust decision category; zero means authorized. */
  std::int32_t trust_status{5};
  /** @brief True only when trust authorization permitted execution. */
  bool trust_authorized{false};
  /** @brief Static explanation of the trust decision. */
  std::string trust_reason{"not authorized"};
  /** @brief Runner selection status. */
  std::int32_t runner_status{ztick_status_internal_error};
  /** @brief Final recorded execution mode. */
  ztick_execution_mode execution_mode{ztick_execution_mode_in_process};
  /** @brief Capability mask required and verified before use. */
  std::uint64_t required_capabilities{0U};
  /** @brief Number of formats returned by `describe`. */
  std::uint32_t format_count{0U};
  /** @brief Number of typed parameters returned by `describe`. */
  std::uint32_t parameter_count{0U};
  /** @brief Number of unsupported configurations returned by `describe`. */
  std::uint32_t unsupported_count{0U};
  /** @brief Number of primary processing blocks in the workload. */
  std::uint32_t block_count{0U};
  /** @brief Number of processed frames across all channels. */
  std::uint64_t processed_frames{0U};
  /** @brief Number of Strict validation checks reported by the adapter. */
  std::uint32_t validation_check_count{0U};
  /** @brief Host-measured whole-run DSP duration in seconds. */
  double whole_run_seconds{0.0};
  /** @brief Sum of adapter-emitted per-block durations in seconds. */
  double per_block_seconds_sum{0.0};
  /** @brief Metrics received through the report sink, empty for child-mode hosts. */
  std::vector<MetricReport> metrics;
  /** @brief Strict validation outcomes, empty for child-mode hosts. */
  std::vector<ValidationReportEntry> validation_results;
};

/**
 * @brief Run the complete dynamic App B end-to-end harness.
 * @param module_path NUL-terminated path to the dynamic App B shared library.
 * @param isolation Isolation mode to select and record.
 * @param child_executable Path to this harness executable for out-of-process mode.
 * @return Result retaining preflight statuses, execution mode and collected evidence.
 * @note The function contains all exceptions and never returns a partially successful
 *       result. In out-of-process mode the detailed evidence is created in the child,
 *       so the host result records only the final IPC status.
 */
auto run_harness(const char *module_path, HarnessIsolation isolation,
                 const char *child_executable = nullptr) noexcept -> HarnessResult;

/**
 * @brief Execute the App B engine lifecycle in a child harness process.
 * @param module_path NUL-terminated path to the dynamic App B shared library.
 * @return `ztick_status_success` only when the lifecycle, Strict validation and metric
 *         reporting all passed.
 * @note This function silently loads the authorized module and runs the workload; the
 *       caller is responsible for publishing the returned status through IPC.
 */
auto run_child_engine(const char *module_path) noexcept -> std::int32_t;

/**
 * @brief Render one harness result as a stable human-readable summary.
 * @param result Result to render.
 * @return Single-line summary suitable for an executable's standard output.
 */
auto format_summary(const HarnessResult &result) -> std::string;

} // namespace ztick::example::app_b

#endif // ZTICK_APP_B_HARNESS_HPP
