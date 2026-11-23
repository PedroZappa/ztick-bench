// SPDX-License-Identifier: MIT
/**
 * @file phase3_integrations_test.cpp
 * @brief End-to-end verification of the Phase 3 SDK integrations.
 *
 * This is the verification pass for ROADMAP P3.10: both custom SDK test
 * applications run end-to-end. It drives the static processor/chain harness and
 * the dynamic engine harness in one process, then exercises the App B child entry
 * point for the isolation path. Every expected count and status is computed from
 * the published contracts in `app_a_contract.hpp` and `app_b_contract.hpp` and
 * from the written validation levels, never from the values reported by the
 * harnesses under test (TESTING_STRATEGY §3).
 */

#include <catch2/catch_test_macros.hpp>

#include <app_a_contract.hpp>
#include <app_a_harness.hpp>
#include <app_b_contract.hpp>
#include <app_b_harness.hpp>

#include <ztick/sdk.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

/**
 * @brief Count reported metrics carrying one provenance status.
 * @tparam Report Metric report type exposing a `status` member.
 * @param reports Reports to inspect.
 * @param status Provenance label to count.
 * @return Number of reports whose status equals @p status.
 */
template <typename Report>
auto count_status(const std::vector<Report> &reports,
                  ztick_metric_status status) noexcept -> std::size_t {
  return static_cast<std::size_t>(
      std::count_if(reports.begin(), reports.end(),
                    [status](const Report &report) noexcept { return report.status == status; }));
}

/**
 * @brief Confirm that every reported validation check completed successfully.
 * @tparam Report Validation report type exposing a `status` member.
 * @param reports Per-check outcomes to inspect.
 * @return True when @p reports is non-empty and every outcome is success.
 */
template <typename Report>
auto all_checks_passed(const std::vector<Report> &reports) noexcept -> bool {
  return !reports.empty() &&
         std::all_of(reports.begin(), reports.end(), [](const Report &report) noexcept {
           return report.status == ztick_status_success;
         });
}

} // namespace

/**
 * @brief Verify that both custom SDK applications run their full lifecycles.
 */
TEST_CASE("phase3 both sdk test apps run end to end", "[sdk][integration][phase3]") {
  using namespace ztick::example;

  // App A: the static descriptor is registered and both workloads are driven
  // through create, configure, prepare, process, finish, validate and report.
  const app_a::HarnessResult static_result = app_a::run_harness();
  REQUIRE(static_result.ok);
  CHECK(static_result.error.empty());
  CHECK(static_result.sdk_version_status == ztick_status_success);
  CHECK(static_result.execution_mode == ztick_execution_mode_in_process);
  CHECK(static_result.format_count >= 4U);
  CHECK(static_result.parameter_count >= 6U);
  CHECK(static_result.unsupported_count >= 4U);

  REQUIRE(static_result.workloads.size() == 2U);
  CHECK(static_result.workloads[0].kind == app_a::WorkloadKind::processor);
  CHECK(static_result.workloads[1].kind == app_a::WorkloadKind::chain);
  CHECK(static_result.workloads[0].ok);
  CHECK(static_result.workloads[1].ok);
  CHECK(static_result.block_count == 2U * app_a::kBlockCount);
  CHECK(static_result.processed_frames ==
        static_cast<std::uint64_t>(2U) * app_a::kBlockCount * app_a::kFramesPerBlock);

  // Five checks per workload for the Standard level: finite, range, reference,
  // expected change and the adapter-specific custom check.
  constexpr std::uint32_t kAppAChecksPerWorkload = 5U;
  CHECK(static_result.validation_check_count == 2U * kAppAChecksPerWorkload);
  CHECK(static_result.validation_results.size() == static_result.validation_check_count);
  CHECK(all_checks_passed(static_result.validation_results));

  // Six metrics per workload: four measured, one unavailable and one not applicable.
  CHECK(static_result.metrics.size() == 12U);
  CHECK(count_status(static_result.metrics, ztick_metric_status_measured) == 8U);
  CHECK(count_status(static_result.metrics, ztick_metric_status_unavailable) == 2U);
  CHECK(count_status(static_result.metrics, ztick_metric_status_not_applicable) == 2U);
  CHECK(static_result.whole_run_seconds >= 0.0);
  CHECK(static_result.per_block_seconds_sum >= 0.0);

  const std::string static_summary = app_a::format_summary(static_result);
  CHECK(static_summary.find("app_a ok=1") == 0U);
  CHECK(static_summary.find("workloads=2") != std::string::npos);

  // App B: the dynamic module is inspected, authorized, loaded and driven
  // through the engine workload with Strict validation and threaded processing.
  const app_b::HarnessResult dynamic_result =
      app_b::run_harness(ZTICK_DYNAMIC_APP_B_PATH, app_b::HarnessIsolation::in_process);
  REQUIRE(dynamic_result.ok);
  CHECK(dynamic_result.error.empty());
  CHECK(dynamic_result.inspection_status == ztick_status_success);
  CHECK(dynamic_result.sdk_version_status == ztick_status_success);
  CHECK(dynamic_result.negotiation_status == ztick_status_success);
  CHECK(dynamic_result.runtime_load_status == ztick_status_success);
  CHECK(dynamic_result.trust_authorized);
  CHECK(dynamic_result.runner_status == ztick_status_success);
  CHECK(dynamic_result.execution_mode == ztick_execution_mode_in_process);
  CHECK(dynamic_result.required_capabilities == app_b::kRequiredCapabilities);
  CHECK(dynamic_result.format_count == 2U);
  CHECK(dynamic_result.parameter_count == 7U);
  CHECK(dynamic_result.block_count == app_b::kBlockCount);
  CHECK(dynamic_result.processed_frames ==
        static_cast<std::uint64_t>(app_b::kBlockCount) * app_b::kFramesPerBlock);

  // Strict adds thread balance and a custom engine check to the five basic ones.
  constexpr std::uint32_t kAppBStrictChecks = 6U;
  CHECK(dynamic_result.validation_check_count == kAppBStrictChecks);
  CHECK(dynamic_result.validation_results.size() == dynamic_result.validation_check_count);
  CHECK(all_checks_passed(dynamic_result.validation_results));

  // The engine reports six measured values plus one of each non-measured status.
  CHECK(dynamic_result.metrics.size() == 9U);
  CHECK(count_status(dynamic_result.metrics, ztick_metric_status_measured) == 6U);
  CHECK(count_status(dynamic_result.metrics, ztick_metric_status_estimated) == 1U);
  CHECK(count_status(dynamic_result.metrics, ztick_metric_status_unavailable) == 1U);
  CHECK(count_status(dynamic_result.metrics, ztick_metric_status_not_applicable) == 1U);
  CHECK(dynamic_result.whole_run_seconds >= 0.0);
  CHECK(dynamic_result.per_block_seconds_sum >= 0.0);

  const std::string dynamic_summary = app_b::format_summary(dynamic_result);
  CHECK(dynamic_summary.find("app_b ok=1") == 0U);
  CHECK(dynamic_summary.find("execution=in_process") != std::string::npos);
}

/**
 * @brief Verify the dynamic app's isolated child path completes end to end.
 */
TEST_CASE("phase3 dynamic app B child engine runs end to end", "[sdk][integration][phase3]") {
  CHECK(ztick::example::app_b::run_child_engine(ZTICK_DYNAMIC_APP_B_PATH) == ztick_status_success);
}
