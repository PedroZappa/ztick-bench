// SPDX-License-Identifier: MIT
/**
 * @file generator_test.cpp
 * @brief Catch2 tests for deterministic generators, config round-trips and output.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <ztick/generator.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

/** @brief Config-building helpers used by the generator tests. */
namespace {

/**
 * @brief Build a sine generator config object.
 * @param amplitude Peak amplitude.
 * @param frequency_hz Frequency in Hz.
 * @param phase_rad Initial phase in radians.
 * @return Config-shaped generator definition.
 */
ztick::ConfigValue make_sine_config(double amplitude, double frequency_hz, double phase_rad) {
  ztick::ConfigValue data = ztick::ConfigValue::object();
  data.set("kind", ztick::ConfigValue::string("sine"));
  data.set("amplitude", ztick::ConfigValue::real(amplitude));
  data.set("frequency_hz", ztick::ConfigValue::real(frequency_hz));
  data.set("phase_rad", ztick::ConfigValue::real(phase_rad));
  return data;
}

/**
 * @brief Build one tone config object.
 * @param frequency_hz Tone frequency in Hz.
 * @param phase_rad Tone phase in radians.
 * @param weight Relative tone weight.
 * @return Config-shaped tone definition.
 */
ztick::ConfigValue make_tone_config(double frequency_hz, double phase_rad, double weight) {
  ztick::ConfigValue data = ztick::ConfigValue::object();
  data.set("frequency_hz", ztick::ConfigValue::real(frequency_hz));
  data.set("phase_rad", ztick::ConfigValue::real(phase_rad));
  data.set("weight", ztick::ConfigValue::real(weight));
  return data;
}

/**
 * @brief Build a multi-tone generator config object.
 * @param amplitude Peak amplitude.
 * @param tones Tone config objects to include.
 * @return Config-shaped generator definition.
 */
ztick::ConfigValue make_multitone_config(double amplitude,
                                         const std::vector<ztick::ConfigValue> &tones) {
  ztick::ConfigValue data = ztick::ConfigValue::object();
  data.set("kind", ztick::ConfigValue::string("multitone"));
  data.set("amplitude", ztick::ConfigValue::real(amplitude));
  data.set("tones", ztick::ConfigValue::array(ztick::ConfigValue::Array{tones}));
  return data;
}

/**
 * @brief Build an impulse generator config object.
 * @param amplitude Peak amplitude.
 * @param position Impulse frame position.
 * @return Config-shaped generator definition.
 */
ztick::ConfigValue make_impulse_config(double amplitude, std::int64_t position) {
  ztick::ConfigValue data = ztick::ConfigValue::object();
  data.set("kind", ztick::ConfigValue::string("impulse"));
  data.set("amplitude", ztick::ConfigValue::real(amplitude));
  data.set("position", ztick::ConfigValue::integer(position));
  return data;
}

/**
 * @brief Compute a sine sample independently from the fixed BENCHMARK §3 formula.
 * @param amplitude Peak amplitude.
 * @param frequency_hz Frequency in Hz.
 * @param phase_rad Initial phase in radians.
 * @param sample_rate_hz Sample rate in Hz.
 * @param frame Zero-based frame index.
 * @return Expected sample value at @p frame.
 */
double expected_sine(double amplitude, double frequency_hz, double phase_rad, double sample_rate_hz,
                     std::size_t frame) {
  const double angle = 2.0 * std::numbers::pi_v<double> * frequency_hz *
                           static_cast<double>(frame) / sample_rate_hz +
                       phase_rad;
  return amplitude * std::sin(angle);
}

} // namespace

/**
 * @brief Verify generator kind tokens parse and render symmetrically.
 */
TEST_CASE("generator type tokens round-trip", "[core][generator]") {
  using ztick::GeneratorType;

  REQUIRE(ztick::parse_generator_type("sine") == GeneratorType::sine);
  REQUIRE(ztick::parse_generator_type("multitone") == GeneratorType::multitone);
  REQUIRE(ztick::parse_generator_type("harmonic") == GeneratorType::multitone);
  REQUIRE(ztick::parse_generator_type("impulse") == GeneratorType::impulse);
  REQUIRE_FALSE(ztick::parse_generator_type("noise").has_value());

  REQUIRE(ztick::generator_type_name(GeneratorType::sine) == "sine");
  REQUIRE(ztick::generator_type_name(GeneratorType::multitone) == "multitone");
  REQUIRE(ztick::generator_type_name(GeneratorType::impulse) == "impulse");
}

/**
 * @brief Verify sine factories reject invalid numeric parameters.
 */
TEST_CASE("sine parameters are validated", "[core][generator]") {
  using ztick::SineParameters;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();

  REQUIRE_FALSE(ztick::GeneratorSpec::sine(SineParameters{-1.0, 440.0, 0.0}).has_value());
  REQUIRE_FALSE(ztick::GeneratorSpec::sine(SineParameters{nan, 440.0, 0.0}).has_value());
  REQUIRE_FALSE(ztick::GeneratorSpec::sine(SineParameters{1.0, inf, 0.0}).has_value());
  REQUIRE_FALSE(ztick::GeneratorSpec::sine(SineParameters{1.0, -1.0, 0.0}).has_value());
  REQUIRE_FALSE(ztick::GeneratorSpec::sine(SineParameters{1.0, 440.0, nan}).has_value());

  const auto spec = ztick::GeneratorSpec::sine(SineParameters{0.5, 1000.0, 0.25});
  REQUIRE(spec.has_value());
  REQUIRE(spec->type() == ztick::GeneratorType::sine);
  REQUIRE(spec->as_sine() != nullptr);
  REQUIRE(spec->as_multitone() == nullptr);
  REQUIRE(spec->as_impulse() == nullptr);
  REQUIRE(spec->as_sine()->amplitude == Catch::Approx(0.5));
}

/**
 * @brief Verify sine output matches the fixed formula and is deterministic.
 */
TEST_CASE("sine samples match the fixed formula", "[core][generator]") {
  const auto spec = ztick::GeneratorSpec::sine(ztick::SineParameters{0.8, 997.0, 0.3});
  REQUIRE(spec.has_value());

  const std::vector<double> samples = ztick::generate_samples(*spec, 48000.0, 32);
  REQUIRE(samples.size() == 32);
  for (std::size_t n = 0; n < samples.size(); ++n) {
    REQUIRE(samples[n] == Catch::Approx(expected_sine(0.8, 997.0, 0.3, 48000.0, n)).margin(1e-12));
  }

  const std::vector<double> again = ztick::generate_samples(*spec, 48000.0, 32);
  REQUIRE(again == samples);
  REQUIRE(ztick::generate_samples(*spec, 48000.0, 0).empty());
  REQUIRE(ztick::generate_samples(*spec, 0.0, 8).empty());
  REQUIRE(ztick::generate_samples(*spec, -48000.0, 8).empty());
  REQUIRE(ztick::generate_samples(*spec, std::numeric_limits<double>::quiet_NaN(), 8).empty());
}

/**
 * @brief Verify a sine generator round-trips through config-shaped data.
 */
TEST_CASE("sine generator round-trips from config", "[core][generator]") {
  const ztick::ConfigValue data = make_sine_config(0.75, 220.0, 1.25);
  std::string error;
  const auto spec = ztick::generator_from_config(data, &error);
  REQUIRE(spec.has_value());
  REQUIRE(spec->as_sine()->amplitude == Catch::Approx(0.75));
  REQUIRE(spec->as_sine()->frequency_hz == Catch::Approx(220.0));
  REQUIRE(spec->as_sine()->phase_rad == Catch::Approx(1.25));
  REQUIRE(ztick::generator_to_config(*spec) == data);

  ztick::ConfigValue integers = make_sine_config(0.75, 220.0, 1.25);
  integers.set("amplitude", ztick::ConfigValue::integer(1));
  const auto from_integer = ztick::generator_from_config(integers);
  REQUIRE(from_integer.has_value());
  REQUIRE(from_integer->as_sine()->amplitude == Catch::Approx(1.0));
}

/**
 * @brief Verify multi-tone weights are normalised and output matches the formula.
 */
TEST_CASE("multitone output matches the fixed formula", "[core][generator]") {
  using ztick::Tone;
  const auto spec = ztick::GeneratorSpec::multitone(
      ztick::MultitoneParameters{0.9, {Tone{440.0, 0.0, 1.0}, Tone{880.0, 0.5, 3.0}}});
  REQUIRE(spec.has_value());

  const auto *parameters = spec->as_multitone();
  REQUIRE(parameters != nullptr);
  REQUIRE(parameters->tones.size() == 2);
  const double weight_sum =
      std::abs(parameters->tones[0].weight) + std::abs(parameters->tones[1].weight);
  REQUIRE(weight_sum == Catch::Approx(1.0));
  REQUIRE(parameters->tones[1].weight == Catch::Approx(3.0 * parameters->tones[0].weight));

  const std::vector<double> samples = ztick::generate_samples(*spec, 48000.0, 16);
  REQUIRE(samples.size() == 16);
  for (std::size_t n = 0; n < samples.size(); ++n) {
    const double first = expected_sine(parameters->tones[0].weight, 440.0, 0.0, 48000.0, n);
    const double second = expected_sine(parameters->tones[1].weight, 880.0, 0.5, 48000.0, n);
    REQUIRE(samples[n] == Catch::Approx(0.9 * (first + second)).margin(1e-12));
    REQUIRE(std::abs(samples[n]) <= 0.9 + 1e-12);
  }
}

/**
 * @brief Verify multi-tone factories reject empty or degenerate tone sets.
 */
TEST_CASE("multitone parameters are validated", "[core][generator]") {
  using ztick::Tone;
  const double nan = std::numeric_limits<double>::quiet_NaN();

  REQUIRE_FALSE(ztick::GeneratorSpec::multitone(ztick::MultitoneParameters{1.0, {}}).has_value());
  REQUIRE_FALSE(
      ztick::GeneratorSpec::multitone(ztick::MultitoneParameters{1.0, {Tone{440.0, 0.0, 0.0}}})
          .has_value());
  REQUIRE_FALSE(
      ztick::GeneratorSpec::multitone(ztick::MultitoneParameters{-1.0, {Tone{440.0, 0.0, 1.0}}})
          .has_value());
  REQUIRE_FALSE(
      ztick::GeneratorSpec::multitone(ztick::MultitoneParameters{1.0, {Tone{nan, 0.0, 1.0}}})
          .has_value());
  REQUIRE_FALSE(
      ztick::GeneratorSpec::multitone(ztick::MultitoneParameters{1.0, {Tone{440.0, 0.0, nan}}})
          .has_value());
  REQUIRE_FALSE(
      ztick::GeneratorSpec::multitone(ztick::MultitoneParameters{1.0, {Tone{-1.0, 0.0, 1.0}}})
          .has_value());

  const auto negative_weight = ztick::GeneratorSpec::multitone(
      ztick::MultitoneParameters{1.0, {Tone{440.0, 0.0, -2.0}, Tone{880.0, 0.0, 1.0}}});
  REQUIRE(negative_weight.has_value());
  REQUIRE(negative_weight->as_multitone()->tones[0].weight == Catch::Approx(-2.0 / 3.0));
}

/**
 * @brief Verify a multi-tone generator round-trips through config-shaped data.
 */
TEST_CASE("multitone generator round-trips from config", "[core][generator]") {
  const ztick::ConfigValue raw = make_multitone_config(
      0.6, {make_tone_config(330.0, 0.1, 2.0), make_tone_config(660.0, 0.2, 1.0)});
  const ztick::ConfigValue normalised = make_multitone_config(
      0.6, {make_tone_config(330.0, 0.1, 2.0 / 3.0), make_tone_config(660.0, 0.2, 1.0 / 3.0)});
  std::string error;
  const auto spec = ztick::generator_from_config(raw, &error);
  REQUIRE(spec.has_value());
  REQUIRE(spec->type() == ztick::GeneratorType::multitone);
  REQUIRE(ztick::generator_to_config(*spec) == normalised);

  ztick::ConfigValue harmonic = raw;
  harmonic.set("kind", ztick::ConfigValue::string("harmonic"));
  const auto alias = ztick::generator_from_config(harmonic);
  REQUIRE(alias.has_value());
  REQUIRE(ztick::generator_to_config(*alias) == normalised);

  const auto replayed = ztick::generator_from_config(normalised);
  REQUIRE(replayed.has_value());
  REQUIRE(*replayed == *spec);
  REQUIRE(ztick::generator_to_config(*replayed) == normalised);
}

/**
 * @brief Verify impulse output matches the fixed definition.
 */
TEST_CASE("impulse samples match the fixed definition", "[core][generator]") {
  const auto spec = ztick::GeneratorSpec::impulse(ztick::ImpulseParameters{0.4, 5});
  REQUIRE(spec.has_value());
  REQUIRE(spec->type() == ztick::GeneratorType::impulse);
  REQUIRE(spec->as_impulse()->position == 5);
  REQUIRE(spec->as_sine() == nullptr);

  const std::vector<double> samples = ztick::generate_samples(*spec, 48000.0, 8);
  REQUIRE(samples.size() == 8);
  for (std::size_t n = 0; n < samples.size(); ++n) {
    REQUIRE(samples[n] == (n == 5 ? Catch::Approx(0.4) : Catch::Approx(0.0)));
  }

  const auto outside = ztick::GeneratorSpec::impulse(ztick::ImpulseParameters{0.4, 8});
  REQUIRE(outside.has_value());
  const std::vector<double> shifted = ztick::generate_samples(*outside, 48000.0, 8);
  REQUIRE(shifted.size() == 8);
  REQUIRE(std::all_of(shifted.begin(), shifted.end(), [](double value) { return value == 0.0; }));
}

/**
 * @brief Verify an impulse generator round-trips through config-shaped data.
 */
TEST_CASE("impulse generator round-trips from config", "[core][generator]") {
  const ztick::ConfigValue data = make_impulse_config(0.4, 12);
  const auto spec = ztick::generator_from_config(data);
  REQUIRE(spec.has_value());
  REQUIRE(spec->as_impulse()->amplitude == Catch::Approx(0.4));
  REQUIRE(spec->as_impulse()->position == 12);
  REQUIRE(ztick::generator_to_config(*spec) == data);

  const auto max_position = static_cast<std::int64_t>(std::numeric_limits<std::int64_t>::max());
  REQUIRE(ztick::generator_from_config(make_impulse_config(1.0, max_position)).has_value());
  const double nan = std::numeric_limits<double>::quiet_NaN();
  REQUIRE_FALSE(ztick::GeneratorSpec::impulse(ztick::ImpulseParameters{nan, 0}).has_value());
  REQUIRE_FALSE(ztick::GeneratorSpec::impulse(
                    ztick::ImpulseParameters{1.0, static_cast<std::size_t>(max_position) + 1})
                    .has_value());
}

/**
 * @brief Verify malformed generator config objects are rejected with diagnostics.
 */
TEST_CASE("generator_from_config rejects malformed data", "[core][generator]") {
  std::string error;

  REQUIRE_FALSE(ztick::generator_from_config(ztick::ConfigValue::null(), &error).has_value());
  REQUIRE_FALSE(error.empty());

  ztick::ConfigValue missing_kind = ztick::ConfigValue::object();
  REQUIRE_FALSE(ztick::generator_from_config(missing_kind, &error).has_value());
  REQUIRE(error.find("kind") != std::string::npos);

  ztick::ConfigValue bad_kind = ztick::ConfigValue::object();
  bad_kind.set("kind", ztick::ConfigValue::integer(1));
  REQUIRE_FALSE(ztick::generator_from_config(bad_kind, &error).has_value());

  ztick::ConfigValue unknown_kind = ztick::ConfigValue::object();
  unknown_kind.set("kind", ztick::ConfigValue::string("noise"));
  REQUIRE_FALSE(ztick::generator_from_config(unknown_kind, &error).has_value());

  ztick::ConfigValue no_amplitude = ztick::ConfigValue::object();
  no_amplitude.set("kind", ztick::ConfigValue::string("sine"));
  REQUIRE_FALSE(ztick::generator_from_config(no_amplitude, &error).has_value());

  ztick::ConfigValue bad_amplitude = make_sine_config(1.0, 440.0, 0.0);
  bad_amplitude.set("amplitude", ztick::ConfigValue::string("loud"));
  REQUIRE_FALSE(ztick::generator_from_config(bad_amplitude, &error).has_value());

  ztick::ConfigValue no_frequency = ztick::ConfigValue::object();
  no_frequency.set("kind", ztick::ConfigValue::string("sine"));
  no_frequency.set("amplitude", ztick::ConfigValue::real(1.0));
  REQUIRE_FALSE(ztick::generator_from_config(no_frequency, &error).has_value());

  ztick::ConfigValue no_tones = ztick::ConfigValue::object();
  no_tones.set("kind", ztick::ConfigValue::string("multitone"));
  no_tones.set("amplitude", ztick::ConfigValue::real(1.0));
  REQUIRE_FALSE(ztick::generator_from_config(no_tones, &error).has_value());

  ztick::ConfigValue bad_tones = make_multitone_config(1.0, {make_tone_config(440.0, 0.0, 1.0)});
  bad_tones.set("tones", ztick::ConfigValue::string("not-an-array"));
  REQUIRE_FALSE(ztick::generator_from_config(bad_tones, &error).has_value());

  ztick::ConfigValue empty_tones = make_multitone_config(1.0, {});
  REQUIRE_FALSE(ztick::generator_from_config(empty_tones, &error).has_value());

  ztick::ConfigValue bad_tone = make_multitone_config(1.0, {make_tone_config(440.0, 0.0, 1.0)});
  bad_tone.set("tones", ztick::ConfigValue::array(ztick::ConfigValue::Array{
                            ztick::ConfigValue::string("not-an-object")}));
  REQUIRE_FALSE(ztick::generator_from_config(bad_tone, &error).has_value());

  ztick::ConfigValue negative_position = make_impulse_config(1.0, -1);
  REQUIRE_FALSE(ztick::generator_from_config(negative_position, &error).has_value());

  ztick::ConfigValue float_position = make_impulse_config(1.0, 1);
  float_position.set("position", ztick::ConfigValue::real(1.5));
  REQUIRE_FALSE(ztick::generator_from_config(float_position, &error).has_value());
}

/**
 * @brief Verify identical recorded configs reproduce identical samples (BR §4.3).
 */
TEST_CASE("recorded configs reproduce identical samples", "[core][generator]") {
  const std::vector<ztick::ConfigValue> configs = {
      make_sine_config(0.5, 1000.0, 0.125),
      make_multitone_config(
          0.5, {make_tone_config(220.0, 0.0, 3.0), make_tone_config(440.0, 0.25, 1.0)}),
      make_impulse_config(0.5, 7),
  };

  for (const ztick::ConfigValue &config : configs) {
    std::string error;
    const auto first = ztick::generator_from_config(config, &error);
    REQUIRE(first.has_value());
    const auto second = ztick::generator_from_config(config);
    REQUIRE(second.has_value());
    REQUIRE(*first == *second);

    const std::vector<double> first_samples = ztick::generate_samples(*first, 44100.0, 64);
    const std::vector<double> replayed = ztick::generate_samples(
        *ztick::generator_from_config(ztick::generator_to_config(*first)), 44100.0, 64);
    REQUIRE(first_samples == replayed);
    REQUIRE(first_samples == ztick::generate_samples(*second, 44100.0, 64));
  }
}
