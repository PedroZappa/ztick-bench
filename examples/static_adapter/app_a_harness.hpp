// SPDX-License-Identifier: MIT
/**
 * @file app_a_harness.hpp
 * @brief Observable results of the static App A end-to-end harness.
 *
 * The harness is the host side of custom test app A: it registers the static
 * adapter, negotiates metadata, drives the complete SDK lifecycle for both a
 * processor and a chain workload, and collects validation and measurement
 * evidence. It depends only on the installed SDK C ABI and the C++ standard
 * library so it can be built outside ztick-bench (BR §17.8, TESTING_STRATEGY §3).
 */

#ifndef ZTICK_APP_A_HARNESS_HPP
#define ZTICK_APP_A_HARNESS_HPP

#include <ztick/sdk.h>

#include <cstdint>
#include <string>
#include <vector>

/** @brief Static processor/chain SDK test application. */
namespace ztick::example::app_a {

/** @brief Workload variant exercised by one App A lifecycle run. */
enum class WorkloadKind : std::uint8_t {
  /** @brief Single gain processor with smoothing disabled. */
  processor = 0,
  /** @brief Two-stage gain chain with smoothing enabled. */
  chain = 1,
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

/** @brief One per-check validation outcome reported by the adapter. */
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

/** @brief Complete observable result of one App A workload run. */
struct WorkloadResult {
  /** @brief Workload variant that was executed. */
  WorkloadKind kind{WorkloadKind::processor};
  /** @brief True only when every workload expectation passed. */
  bool ok{false};
  /** @brief First failed workload expectation, or empty on success. */
  std::string error;
  /** @brief Number of primary processing blocks in this workload. */
  std::uint32_t block_count{0U};
  /** @brief Number of processed frames across all channels. */
  std::uint64_t processed_frames{0U};
  /** @brief Number of Standard validation checks reported by the adapter. */
  std::uint32_t validation_check_count{0U};
  /** @brief Host-measured whole-workload DSP duration in seconds. */
  double whole_run_seconds{0.0};
  /** @brief Sum of adapter-emitted per-block durations in seconds. */
  double per_block_seconds_sum{0.0};
  /** @brief Metrics received through the report sink. */
  std::vector<MetricReport> metrics;
  /** @brief Standard-level validation outcomes received from the adapter. */
  std::vector<ValidationReportEntry> validation_results;
};

/** @brief Complete observable result of one App A harness run. */
struct HarnessResult {
  /** @brief True only when every harness expectation passed. */
  bool ok{false};
  /** @brief First failed expectation, or an empty string on success. */
  std::string error;
  /** @brief Status returned by the SDK version query. */
  std::int32_t sdk_version_status{ztick_status_internal_error};
  /** @brief Static-registration status; duplicate is tolerated in a shared process. */
  std::int32_t registration_status{ztick_status_internal_error};
  /** @brief Execution mode recorded by the harness. */
  ztick_execution_mode execution_mode{ztick_execution_mode_in_process};
  /** @brief Capability mask required and verified before use. */
  std::uint64_t required_capabilities{0U};
  /** @brief Number of formats returned by @ref ztick_adapter_describe_fn. */
  std::uint32_t format_count{0U};
  /** @brief Number of typed parameters returned by @ref ztick_adapter_describe_fn. */
  std::uint32_t parameter_count{0U};
  /** @brief Number of unsupported configurations returned by describe. */
  std::uint32_t unsupported_count{0U};
  /** @brief Number of validation checks across both workloads. */
  std::uint32_t validation_check_count{0U};
  /** @brief Total number of primary blocks processed. */
  std::uint32_t block_count{0U};
  /** @brief Total number of processed frames across both workloads. */
  std::uint64_t processed_frames{0U};
  /** @brief Host-measured whole-run DSP duration in seconds. */
  double whole_run_seconds{0.0};
  /** @brief Sum of adapter-emitted per-block durations in seconds. */
  double per_block_seconds_sum{0.0};
  /** @brief Metrics received from both workloads through the report sink. */
  std::vector<MetricReport> metrics;
  /** @brief Standard-level validation outcomes from both workloads. */
  std::vector<ValidationReportEntry> validation_results;
  /** @brief Per-workload results in processor-then-chain order. */
  std::vector<WorkloadResult> workloads;
};

/**
 * @brief Run the complete static App A end-to-end harness.
 * @return Result retaining statuses, counts, validation outcomes and metrics for
 *         both the processor and chain workloads.
 * @note The function contains all exceptions and never returns a partially
 *       successful result. It registers the static adapter once, then creates a
 *       separate instance for each workload through the descriptor's lifecycle.
 */
auto run_harness() noexcept -> HarnessResult;

/**
 * @brief Render one harness result as a stable human-readable summary.
 * @param result Result to render.
 * @return Single-line summary suitable for an executable's standard output.
 */
auto format_summary(const HarnessResult &result) -> std::string;

} // namespace ztick::example::app_a

#endif // ZTICK_APP_A_HARNESS_HPP
