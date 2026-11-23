// SPDX-License-Identifier: MIT
/**
 * @file app_b_contract.hpp
 * @brief Stable constants shared by the dynamic App B adapter and its host harness.
 *
 * The contract uses fixed-width C++ constants and C strings so the shared library
 * and the host can implement the same engine workload without a private ABI. The
 * engine formula is stateless, which lets the adapter divide each block across its
 * own worker threads while producing deterministic samples (SDK_SPECIFICATION §3,
 * §4). This header is internal to the in-tree example and is not installed.
 */

#ifndef ZTICK_APP_B_CONTRACT_HPP
#define ZTICK_APP_B_CONTRACT_HPP

#include <ztick/types.h>

#include <cstdint>

/** @brief Dynamic engine SDK test application. */
namespace ztick::example::app_b {

/** @brief Stable registry identifier of the dynamic App B adapter. */
inline constexpr const char kAdapterId[] = "ztick.test.dynamic.app_b";

/** @brief Human-readable name of the dynamic App B adapter. */
inline constexpr const char kAdapterName[] = "Dynamic engine SDK test adapter";

/** @brief Extra metadata recorded in the adapter descriptor. */
inline constexpr const char kAdapterMetadata[] =
    "P3.8 dynamic engine harness with internal worker threads, instrumented "
    "measurements, adapter references and Strict validation";

/** @brief Major component of the example integration version. */
inline constexpr std::uint32_t kAdapterVersionMajor = 1U;
/** @brief Minor component of the example integration version. */
inline constexpr std::uint32_t kAdapterVersionMinor = 0U;
/** @brief Patch component of the example integration version. */
inline constexpr std::uint32_t kAdapterVersionPatch = 0U;

/** @brief Capabilities required by the App B harness before configuration. */
inline constexpr std::uint64_t kRequiredCapabilities =
    ZTICK_CAPABILITY_ENGINE | ZTICK_CAPABILITY_PER_BLOCK_TIMING |
    ZTICK_CAPABILITY_INTERNAL_THREADING | ZTICK_CAPABILITY_REFERENCE_DATA |
    ZTICK_CAPABILITY_CUSTOM_VALIDATION;

/** @brief Typed integer parameter selecting the number of synthesized voices. */
inline constexpr const char kParameterVoices[] = "voices";
/** @brief Typed integer parameter selecting the number of shaping stages. */
inline constexpr const char kParameterStages[] = "stages";
/** @brief Typed integer parameter selecting the adapter's worker-thread count. */
inline constexpr const char kParameterThreads[] = "threads";
/** @brief Typed floating-point parameter mixing source and synthesized signal. */
inline constexpr const char kParameterMix[] = "mix";
/** @brief Typed boolean parameter enabling deterministic low-level dither. */
inline constexpr const char kParameterDither[] = "dither";
/** @brief Typed enumeration parameter selecting the shaping curve. */
inline constexpr const char kParameterQuality[] = "quality";
/** @brief Typed string label retained by the adapter for diagnostics. */
inline constexpr const char kParameterProfile[] = "profile";

/** @brief Enumeration value selecting the linear shaping curve. */
inline constexpr const char kQualityDraft[] = "draft";
/** @brief Enumeration value selecting the saturating shaping curve. */
inline constexpr const char kQualityFinal[] = "final";

/** @brief Basic check that every output sample is finite. */
inline constexpr const char kCheckFinite[] = "finite_output";
/** @brief Basic check that every output sample lies in the declared range. */
inline constexpr const char kCheckRange[] = "range_output";
/** @brief Strict check against adapter-generated reference data. */
inline constexpr const char kCheckReference[] = "reference_match";
/** @brief Strict check that the configured engine parameters change the output. */
inline constexpr const char kCheckExpectedChange[] = "expected_change";
/** @brief Strict check that every configured worker thread contributed. */
inline constexpr const char kCheckThreadBalance[] = "thread_balance";
/** @brief Adapter-specific strict output check. */
inline constexpr const char kCheckCustom[] = "custom_engine";

/** @brief Metric name for processed per-channel frames. */
inline constexpr const char kMetricFrames[] = "app_b.frames";
/** @brief Metric name for processed blocks. */
inline constexpr const char kMetricBlocks[] = "app_b.blocks";
/** @brief Metric name for host-measured DSP seconds. */
inline constexpr const char kMetricProcessingSeconds[] = "app_b.processing_seconds";
/** @brief Metric name for summed adapter-instrumented worker seconds. */
inline constexpr const char kMetricWorkerSeconds[] = "app_b.worker_seconds";
/** @brief Metric name for one worker invocation per block and thread. */
inline constexpr const char kMetricWorkerCalls[] = "app_b.worker_calls";
/** @brief Metric name for the largest absolute output sample. */
inline constexpr const char kMetricOutputPeak[] = "app_b.output_peak";
/** @brief Metric name for the deliberately unavailable scaling value. */
inline constexpr const char kMetricScalingEfficiency[] = "app_b.scaling_efficiency";
/** @brief Metric name for the deliberately not-applicable reference-file path. */
inline constexpr const char kMetricReferenceFiles[] = "app_b.reference_files";

/** @brief Unit string for frame counts. */
inline constexpr const char kUnitFrames[] = "frames";
/** @brief Unit string for block counts. */
inline constexpr const char kUnitBlocks[] = "blocks";
/** @brief Unit string for seconds. */
inline constexpr const char kUnitSeconds[] = "s";
/** @brief Unit string for worker invocations. */
inline constexpr const char kUnitCalls[] = "calls";

/** @brief Sample rate used by the App B engine workload, in Hz. */
inline constexpr std::uint32_t kSampleRateHz = 48000U;
/** @brief Channel count used by the App B engine workload. */
inline constexpr std::uint32_t kChannels = 4U;
/** @brief Frame count in each App B processing block. */
inline constexpr std::uint32_t kFramesPerBlock = 256U;
/** @brief Number of primary processing blocks in one App B run. */
inline constexpr std::uint32_t kBlockCount = 8U;
/** @brief Voice count configured by the harness. */
inline constexpr std::int64_t kVoiceCount = 4;
/** @brief Stage count configured by the harness. */
inline constexpr std::int64_t kStageCount = 3;
/** @brief Worker-thread count configured by the harness. */
inline constexpr std::int64_t kThreadCount = 2;
/** @brief Source/synthesis mix configured by the harness. */
inline constexpr double kMix = 0.5;
/** @brief Whether deterministic dither is configured by the harness. */
inline constexpr bool kDither = false;
/** @brief Enumeration index of the final shaping curve. */
inline constexpr std::uint32_t kQualityFinalIndex = 1U;
/** @brief Instance profile label configured by the harness. */
inline constexpr const char kProfile[] = "app_b_engine";

/** @brief Base frequency of the first synthesized voice, in Hz. */
inline constexpr double kBaseFrequencyHz = 220.0;
/** @brief Phase increment applied per voice, in radians. */
inline constexpr double kVoicePhaseStep = 0.125;
/** @brief Phase increment applied per channel, in radians. */
inline constexpr double kChannelPhaseStep = 0.25;
/** @brief Deterministic dither peak amplitude. */
inline constexpr double kDitherAmplitude = 1e-6;
/** @brief Peak scale applied to every produced sample. */
inline constexpr double kOutputScale = 0.8;
/** @brief Highest absolute value accepted by the range check. */
inline constexpr double kRangeLimit = 1.0;

/** @brief Absolute tolerance used for App B reference comparisons. */
inline constexpr double kAbsoluteTolerance = 1e-6;
/** @brief Relative tolerance used for App B reference comparisons. */
inline constexpr double kRelativeTolerance = 1e-6;

/** @brief Child-process mode argument that selects the App B engine child. */
inline constexpr const char kChildModeArgument[] = "engine-child";

} // namespace ztick::example::app_b

#endif // ZTICK_APP_B_CONTRACT_HPP
