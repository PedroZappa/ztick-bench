// SPDX-License-Identifier: MIT
/**
 * @file app_a_contract.hpp
 * @brief Stable constants shared by the static App A adapter and harness.
 *
 * The contract uses only fixed-width C++ constants and C strings so both the
 * adapter and its host can describe the same lifecycle without depending on a
 * private core API (SDK_SPECIFICATION §1, §4). Reference samples are generated
 * and supplied by the host, so the adapter does not declare the
 * adapter-provided reference-data capability. It is internal to the in-tree
 * example and is not installed.
 */

#ifndef ZTICK_APP_A_CONTRACT_HPP
#define ZTICK_APP_A_CONTRACT_HPP

#include <ztick/types.h>

#include <cstdint>

/** @brief Static processor/chain SDK test application. */
namespace ztick::example::app_a {

/** @brief Stable registry identifier of the static App A adapter. */
inline constexpr const char kAdapterId[] = "ztick.test.static.app_a";

/** @brief Human-readable name of the static App A adapter. */
inline constexpr const char kAdapterName[] = "Static processor/chain SDK test adapter";

/** @brief Extra metadata recorded in the adapter descriptor. */
inline constexpr const char kAdapterMetadata[] =
    "P3.7 static processor/chain harness with typed parameters, reference data, "
    "per-block timing and Basic/Standard validation";

/** @brief Major component of the example integration version. */
inline constexpr std::uint32_t kAdapterVersionMajor = 1U;
/** @brief Minor component of the example integration version. */
inline constexpr std::uint32_t kAdapterVersionMinor = 0U;
/** @brief Patch component of the example integration version. */
inline constexpr std::uint32_t kAdapterVersionPatch = 0U;

/** @brief Capabilities required by the App A harness before configuration. */
inline constexpr std::uint64_t kRequiredCapabilities =
    ZTICK_CAPABILITY_PROCESSOR | ZTICK_CAPABILITY_CHAIN | ZTICK_CAPABILITY_PER_BLOCK_TIMING |
    ZTICK_CAPABILITY_CUSTOM_VALIDATION;

/** @brief Typed parameter selecting the processor or chain workload. */
inline constexpr const char kParameterWorkload[] = "workload";
/** @brief Typed floating-point gain applied by each stage. */
inline constexpr const char kParameterGain[] = "gain";
/** @brief Typed integer count of gain stages. */
inline constexpr const char kParameterStages[] = "stages";
/** @brief Typed boolean enabling the chain's smoothing stage. */
inline constexpr const char kParameterSmoothing[] = "smoothing";
/** @brief Typed cutoff parameter for the smoothing stage, in Hz. */
inline constexpr const char kParameterCutoffHz[] = "cutoff_hz";
/** @brief Typed string label copied into the adapter instance. */
inline constexpr const char kParameterLabel[] = "label";

/** @brief Enumeration value selecting the single-processor workload. */
inline constexpr const char kWorkloadProcessor[] = "processor";
/** @brief Enumeration value selecting the two-stage chain workload. */
inline constexpr const char kWorkloadChain[] = "chain";

/** @brief Basic check that every output sample is finite. */
inline constexpr const char kCheckFinite[] = "finite_output";
/** @brief Basic check that every output sample lies in the declared range. */
inline constexpr const char kCheckRange[] = "range_output";
/** @brief Standard check against host-provided reference data. */
inline constexpr const char kCheckReference[] = "reference_match";
/** @brief Standard check that configuration changed the output. */
inline constexpr const char kCheckExpectedChange[] = "expected_change";
/** @brief Adapter-specific output check exercised by App A. */
inline constexpr const char kCheckCustom[] = "custom_output";

/** @brief Metric name for processed per-channel frames. */
inline constexpr const char kMetricFrames[] = "app_a.frames";
/** @brief Metric name for processed blocks. */
inline constexpr const char kMetricBlocks[] = "app_a.blocks";
/** @brief Metric name for adapter-measured DSP seconds. */
inline constexpr const char kMetricProcessingSeconds[] = "app_a.processing_seconds";
/** @brief Metric name for the largest absolute output sample. */
inline constexpr const char kMetricOutputPeak[] = "app_a.output_peak";
/** @brief Metric name for unavailable calibration overhead. */
inline constexpr const char kMetricOverheadSeconds[] = "app_a.overhead_seconds";
/** @brief Metric name for the deliberately not-applicable scaling value. */
inline constexpr const char kMetricScalingEfficiency[] = "app_a.scaling_efficiency";

/** @brief Unit string for frame counts. */
inline constexpr const char kUnitFrames[] = "frames";
/** @brief Unit string for block counts. */
inline constexpr const char kUnitBlocks[] = "blocks";
/** @brief Unit string for seconds. */
inline constexpr const char kUnitSeconds[] = "s";
/** @brief Unit string for Hz. */
inline constexpr const char kUnitHertz[] = "Hz";

/** @brief Sample rate used by the App A workload. */
inline constexpr std::uint32_t kSampleRateHz = 48000U;
/** @brief Channel count used by the App A workload. */
inline constexpr std::uint32_t kChannels = 2U;
/** @brief Frame count in each App A processing block. */
inline constexpr std::uint32_t kFramesPerBlock = 128U;
/** @brief Number of primary processing blocks in one App A run. */
inline constexpr std::uint32_t kBlockCount = 8U;
/** @brief Gain configured by the harness. */
inline constexpr double kGain = 1.25;
/** @brief Stage count configured by the harness. */
inline constexpr std::int64_t kStages = 2;
/** @brief Whether the harness enables the smoothing stage. */
inline constexpr bool kSmoothing = true;
/** @brief Low-pass cutoff configured by the harness, in Hz. */
inline constexpr double kCutoffHz = 3000.0;

/** @brief Absolute tolerance used for App A reference comparisons. */
inline constexpr double kAbsoluteTolerance = 1e-4;
/** @brief Relative tolerance used for App A reference comparisons. */
inline constexpr double kRelativeTolerance = 1e-5;

} // namespace ztick::example::app_a

#endif // ZTICK_APP_A_CONTRACT_HPP
