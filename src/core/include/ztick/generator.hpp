// SPDX-License-Identifier: MIT
/**
 * @file generator.hpp
 * @brief Deterministic input generators and their fixed config representation.
 *
 * BENCHMARK_SPECIFICATION §3 fixes the waveform definitions; BR §4.3 requires
 * generation parameters to be recorded so compared builds receive identical
 * input data. A @ref ztick::GeneratorSpec stores only validated parameters, so
 * the recorded config is exactly the definition used to produce samples.
 * This header is private to ztick-bench and is not installed.
 */

#ifndef ZTICK_GENERATOR_HPP
#define ZTICK_GENERATOR_HPP

#include "ztick/config_value.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ztick {

/** @brief Built-in deterministic generator kinds (REQ-BENCH-02). */
enum class GeneratorType : std::uint8_t {
  /** @brief One sinusoid: `A * sin(2*pi*f*n/fs + phi)`. */
  sine = 0,
  /** @brief Weighted sum of sinusoids whose absolute weights sum to one. */
  multitone = 1,
  /** @brief One non-zero sample at a recorded frame position. */
  impulse = 2,
};

/**
 * @brief Parse a config token into a @ref GeneratorType.
 * @param token Token from the config document, such as `sine`.
 * @return The matching kind, `multitone` for `harmonic`, or `std::nullopt` for
 *         an unknown token.
 */
std::optional<GeneratorType> parse_generator_type(std::string_view token) noexcept;

/**
 * @brief Render a @ref GeneratorType as its config token.
 * @param type Kind to render.
 * @return Stable token used by the config document.
 */
std::string_view generator_type_name(GeneratorType type) noexcept;

/** @brief Fixed parameters for @ref GeneratorType::sine. */
struct SineParameters {
  /** @brief Peak amplitude `A` in sample units; must be finite and non-negative. */
  double amplitude{1.0};
  /** @brief Frequency `f` in Hz; must be finite and non-negative. */
  double frequency_hz{440.0};
  /** @brief Initial phase `phi` in radians; must be finite. */
  double phase_rad{0.0};

  /**
   * @brief Compare two sine parameter sets.
   * @param other Parameter set to compare against.
   * @return True when all three parameters are equal.
   */
  bool operator==(const SineParameters &other) const noexcept = default;
};

/** @brief One component of a @ref MultitoneParameters signal. */
struct Tone {
  /** @brief Frequency `f_k` in Hz; must be finite and non-negative. */
  double frequency_hz{0.0};
  /** @brief Initial phase `phi_k` in radians; must be finite. */
  double phase_rad{0.0};
  /** @brief Relative weight `a_k`; normalised by the absolute-weight sum. */
  double weight{0.0};

  /**
   * @brief Compare two tones.
   * @param other Tone to compare against.
   * @return True when all three components are equal.
   */
  bool operator==(const Tone &other) const noexcept = default;
};

/** @brief Fixed parameters for @ref GeneratorType::multitone. */
struct MultitoneParameters {
  /** @brief Peak amplitude `A` in sample units; must be finite and non-negative. */
  double amplitude{1.0};
  /** @brief Tones to sum; must be non-empty with a positive absolute-weight sum. */
  std::vector<Tone> tones;

  /**
   * @brief Compare two multi-tone parameter sets.
   * @param other Parameter set to compare against.
   * @return True when amplitude and every tone are equal.
   */
  bool operator==(const MultitoneParameters &other) const = default;
};

/** @brief Fixed parameters for @ref GeneratorType::impulse. */
struct ImpulseParameters {
  /** @brief Peak amplitude `A` in sample units; must be finite and non-negative. */
  double amplitude{1.0};
  /** @brief Frame index `p` holding the impulse; must fit a signed 64-bit integer. */
  std::size_t position{0};

  /**
   * @brief Compare two impulse parameter sets.
   * @param other Parameter set to compare against.
   * @return True when amplitude and position are equal.
   */
  bool operator==(const ImpulseParameters &other) const noexcept = default;
};

/**
 * @brief A validated, recordable definition of one built-in generator.
 * @note Every factory validates its inputs and (for multi-tone) normalises
 *       weights, so a constructed spec always reproduces exactly the config
 *       rendered by @ref generator_to_config.
 */
class GeneratorSpec {
public:
  /** @brief Parameter alternatives, ordered to match @ref GeneratorType. */
  using Parameters = std::variant<SineParameters, MultitoneParameters, ImpulseParameters>;

  /**
   * @brief Create a sine generator definition.
   * @param parameters Parameters to validate and record.
   * @return The definition, or `std::nullopt` when a value is non-finite or
   *         a magnitude is negative.
   */
  static std::optional<GeneratorSpec> sine(SineParameters parameters) noexcept;

  /**
   * @brief Create a multi-tone generator definition with normalised weights.
   * @param parameters Parameters to validate; weights are divided by their
   *        absolute-weight sum.
   * @return The definition, or `std::nullopt` when tones are empty, a value is
   *         non-finite, a magnitude is negative, or the weight sum is zero.
   */
  static std::optional<GeneratorSpec> multitone(MultitoneParameters parameters) noexcept;

  /**
   * @brief Create an impulse generator definition.
   * @param parameters Parameters to validate and record.
   * @return The definition, or `std::nullopt` when amplitude is non-finite or
   *         negative, or the position exceeds the signed 64-bit range.
   */
  static std::optional<GeneratorSpec> impulse(ImpulseParameters parameters) noexcept;

  /**
   * @brief Read the discriminant of the stored parameters.
   * @return The generator kind matching the active @ref Parameters alternative.
   */
  [[nodiscard]] GeneratorType type() const noexcept;

  /**
   * @brief Read sine parameters.
   * @return Pointer to the parameters, or `nullptr` for another kind.
   */
  [[nodiscard]] const SineParameters *as_sine() const noexcept;

  /**
   * @brief Read multi-tone parameters.
   * @return Pointer to the parameters, or `nullptr` for another kind.
   */
  [[nodiscard]] const MultitoneParameters *as_multitone() const noexcept;

  /**
   * @brief Read impulse parameters.
   * @return Pointer to the parameters, or `nullptr` for another kind.
   */
  [[nodiscard]] const ImpulseParameters *as_impulse() const noexcept;

  /**
   * @brief Read the recorded parameter variant.
   * @return Const reference to the active parameters.
   */
  [[nodiscard]] const Parameters &parameters() const noexcept;

  /**
   * @brief Compare two generator definitions.
   * @param other Definition to compare against.
   * @return True when both kinds and all recorded parameters are equal.
   */
  bool operator==(const GeneratorSpec &other) const = default;

private:
  /**
   * @brief Construct from already-validated parameters.
   * @param parameters Parameters to move into the definition.
   */
  explicit GeneratorSpec(Parameters parameters) noexcept;

  /** @brief Validated parameters for exactly one generator kind. */
  Parameters parameters_;
};

/**
 * @brief Render a generator definition as the `generator` config object.
 * @param spec Definition to render.
 * @return Object holding `kind` and every fixed parameter needed to reproduce it.
 */
ConfigValue generator_to_config(const GeneratorSpec &spec);

/**
 * @brief Build a generator definition from a config-shaped object.
 * @param data Config-shaped object holding generator settings.
 * @param error Optional receiver for a human-readable failure explanation.
 * @return The validated definition, or `std::nullopt` when the data is malformed.
 * @note A malformed object never yields a partially populated definition.
 */
std::optional<GeneratorSpec> generator_from_config(const ConfigValue &data,
                                                   std::string *error = nullptr);

/**
 * @brief Generate one sample per frame using the fixed definitions in BENCHMARK §3.
 * @param spec Validated generator definition.
 * @param sample_rate_hz Sample rate in Hz; must be finite and positive.
 * @param frames Number of frames to generate.
 * @return One double per frame in frame order, or an empty vector when the
 *         sample rate is invalid or @p frames is zero.
 * @note Generation is deterministic and stateless: identical arguments always
 *       produce bit-identical output, so no random seed is involved.
 */
std::vector<double> generate_samples(const GeneratorSpec &spec, double sample_rate_hz,
                                     std::size_t frames);

} // namespace ztick

#endif // ZTICK_GENERATOR_HPP
