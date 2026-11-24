// SPDX-License-Identifier: MIT
/**
 * @file generator.cpp
 * @brief Deterministic generator definitions, config mapping and sample output.
 */

#include "ztick/generator.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ztick {

namespace {

/**
 * @brief Store a diagnostic in an optional error receiver.
 * @param error Receiver to write, or `nullptr` to ignore the diagnostic.
 * @param message Explanation to store.
 */
void record_error(std::string *error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

/**
 * @brief Require a numeric config member, accepting integers and reals.
 * @param section Config-shaped object to read.
 * @param key Member name.
 * @param label Diagnostic label for the member.
 * @param value Receives the number on success.
 * @param error Optional diagnostic receiver.
 * @return True when the member exists and is numeric.
 */
bool require_number(const ConfigValue &section, std::string_view key, const char *label,
                    double &value, std::string *error) {
  const ConfigValue *member = section.find(key);
  if (member == nullptr) {
    record_error(error, std::string{label} + " is required");
    return false;
  }
  if (member->type() == ConfigValueType::real) {
    const auto real = member->as_real();
    if (real.has_value()) {
      value = *real;
      return true;
    }
  }
  if (member->type() == ConfigValueType::integer) {
    const auto integer = member->as_integer();
    if (integer.has_value()) {
      value = static_cast<double>(*integer);
      return true;
    }
  }
  record_error(error, std::string{label} + " must be a number");
  return false;
}

/**
 * @brief Require a non-negative integer config member.
 * @param section Config-shaped object to read.
 * @param value Receives the position on success.
 * @param error Optional diagnostic receiver.
 * @return True when the member exists, is an integer and is non-negative.
 */
bool require_position(const ConfigValue &section, std::size_t &value, std::string *error) {
  const ConfigValue *member = section.find("position");
  if (member == nullptr) {
    record_error(error, "generator.position is required");
    return false;
  }
  const auto integer = member->as_integer();
  if (!integer.has_value()) {
    record_error(error, "generator.position must be an integer");
    return false;
  }
  if (*integer < 0) {
    record_error(error, "generator.position must be non-negative");
    return false;
  }
  value = static_cast<std::size_t>(*integer);
  return true;
}

/**
 * @brief Parse one tone entry of a multi-tone config object.
 * @param data Config-shaped object describing one tone.
 * @param tone Receives the parsed tone on success.
 * @param error Optional diagnostic receiver.
 * @return True when all three tone members are present and numeric.
 */
bool parse_tone(const ConfigValue &data, Tone &tone, std::string *error) {
  if (data.type() != ConfigValueType::object) {
    record_error(error, "generator.tones entries must be objects");
    return false;
  }
  return require_number(data, "frequency_hz", "tone.frequency_hz", tone.frequency_hz, error) &&
         require_number(data, "phase_rad", "tone.phase_rad", tone.phase_rad, error) &&
         require_number(data, "weight", "tone.weight", tone.weight, error);
}

/**
 * @brief Render a finite number as a real config value.
 * @param value Number to render.
 * @return Config-shaped real holding @p value.
 */
ConfigValue number_to_config(double value) noexcept { return ConfigValue::real(value); }

} // namespace

GeneratorSpec::GeneratorSpec(Parameters parameters) noexcept : parameters_(std::move(parameters)) {}

std::optional<GeneratorType> parse_generator_type(std::string_view token) noexcept {
  if (token == "sine") {
    return GeneratorType::sine;
  }
  if (token == "multitone" || token == "harmonic") {
    return GeneratorType::multitone;
  }
  if (token == "impulse") {
    return GeneratorType::impulse;
  }
  return std::nullopt;
}

std::string_view generator_type_name(GeneratorType type) noexcept {
  switch (type) {
  case GeneratorType::sine:
    return "sine";
  case GeneratorType::multitone:
    return "multitone";
  case GeneratorType::impulse:
    return "impulse";
  }
  return "unknown";
}

std::optional<GeneratorSpec> GeneratorSpec::sine(SineParameters parameters) noexcept {
  if (!std::isfinite(parameters.amplitude) || parameters.amplitude < 0.0 ||
      !std::isfinite(parameters.frequency_hz) || parameters.frequency_hz < 0.0 ||
      !std::isfinite(parameters.phase_rad)) {
    return std::nullopt;
  }
  return GeneratorSpec(Parameters{parameters});
}

std::optional<GeneratorSpec> GeneratorSpec::multitone(MultitoneParameters parameters) noexcept {
  if (!std::isfinite(parameters.amplitude) || parameters.amplitude < 0.0 ||
      parameters.tones.empty()) {
    return std::nullopt;
  }
  double total_weight = 0.0;
  for (const Tone &tone : parameters.tones) {
    if (!std::isfinite(tone.frequency_hz) || tone.frequency_hz < 0.0 ||
        !std::isfinite(tone.phase_rad) || !std::isfinite(tone.weight)) {
      return std::nullopt;
    }
    total_weight += std::abs(tone.weight);
  }
  if (!std::isfinite(total_weight) || !(total_weight > 0.0)) {
    return std::nullopt;
  }
  for (Tone &tone : parameters.tones) {
    tone.weight /= total_weight;
  }
  return GeneratorSpec(Parameters{std::move(parameters)});
}

std::optional<GeneratorSpec> GeneratorSpec::impulse(ImpulseParameters parameters) noexcept {
  const auto max_position = static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max());
  if (!std::isfinite(parameters.amplitude) || parameters.amplitude < 0.0 ||
      parameters.position > max_position) {
    return std::nullopt;
  }
  return GeneratorSpec(Parameters{parameters});
}

GeneratorType GeneratorSpec::type() const noexcept {
  return static_cast<GeneratorType>(parameters_.index());
}

// Public library API: exercised by the test target, which is outside the
// cppcheck scan of include/, src/ and tools/.
// cppcheck-suppress unusedFunction
const SineParameters *GeneratorSpec::as_sine() const noexcept {
  return std::get_if<SineParameters>(&parameters_);
}

// cppcheck-suppress unusedFunction
const MultitoneParameters *GeneratorSpec::as_multitone() const noexcept {
  return std::get_if<MultitoneParameters>(&parameters_);
}

// cppcheck-suppress unusedFunction
const ImpulseParameters *GeneratorSpec::as_impulse() const noexcept {
  return std::get_if<ImpulseParameters>(&parameters_);
}

const GeneratorSpec::Parameters &GeneratorSpec::parameters() const noexcept { return parameters_; }

// cppcheck-suppress unusedFunction
ConfigValue generator_to_config(const GeneratorSpec &spec) {
  ConfigValue data = ConfigValue::object();
  data.set("kind", ConfigValue::string(std::string{generator_type_name(spec.type())}));

  if (spec.type() == GeneratorType::sine) {
    const SineParameters &sine = std::get<SineParameters>(spec.parameters());
    data.set("amplitude", number_to_config(sine.amplitude));
    data.set("frequency_hz", number_to_config(sine.frequency_hz));
    data.set("phase_rad", number_to_config(sine.phase_rad));
  } else if (spec.type() == GeneratorType::multitone) {
    const MultitoneParameters &multitone = std::get<MultitoneParameters>(spec.parameters());
    data.set("amplitude", number_to_config(multitone.amplitude));
    ConfigValue::Array tones;
    tones.reserve(multitone.tones.size());
    std::transform(multitone.tones.begin(), multitone.tones.end(), std::back_inserter(tones),
                   [](const Tone &tone) {
                     ConfigValue entry = ConfigValue::object();
                     entry.set("frequency_hz", number_to_config(tone.frequency_hz));
                     entry.set("phase_rad", number_to_config(tone.phase_rad));
                     entry.set("weight", number_to_config(tone.weight));
                     return entry;
                   });
    data.set("tones", ConfigValue::array(std::move(tones)));
  } else if (spec.type() == GeneratorType::impulse) {
    const ImpulseParameters &impulse = std::get<ImpulseParameters>(spec.parameters());
    data.set("amplitude", number_to_config(impulse.amplitude));
    data.set("position", ConfigValue::integer(static_cast<std::int64_t>(impulse.position)));
  }
  return data;
}

std::optional<GeneratorSpec> generator_from_config(const ConfigValue &data, std::string *error) {
  if (data.type() != ConfigValueType::object) {
    record_error(error, "generator config must be an object");
    return std::nullopt;
  }

  const ConfigValue *kind = data.find("kind");
  if (kind == nullptr) {
    record_error(error, "generator.kind is required");
    return std::nullopt;
  }
  const auto token = kind->as_string();
  if (!token.has_value()) {
    record_error(error, "generator.kind must be a string");
    return std::nullopt;
  }
  const auto type = parse_generator_type(*token);
  if (!type.has_value()) {
    record_error(error, "generator.kind has an unknown value: " + *token);
    return std::nullopt;
  }

  double amplitude = 0.0;
  if (!require_number(data, "amplitude", "generator.amplitude", amplitude, error)) {
    return std::nullopt;
  }

  switch (*type) {
  case GeneratorType::sine: {
    double frequency_hz = 0.0;
    double phase_rad = 0.0;
    if (!require_number(data, "frequency_hz", "generator.frequency_hz", frequency_hz, error) ||
        !require_number(data, "phase_rad", "generator.phase_rad", phase_rad, error)) {
      return std::nullopt;
    }
    return GeneratorSpec::sine(SineParameters{amplitude, frequency_hz, phase_rad});
  }
  case GeneratorType::multitone: {
    const ConfigValue *tones_value = data.find("tones");
    if (tones_value == nullptr) {
      record_error(error, "generator.tones is required");
      return std::nullopt;
    }
    const auto *entries = tones_value->as_array();
    if (entries == nullptr) {
      record_error(error, "generator.tones must be an array");
      return std::nullopt;
    }
    if (entries->empty()) {
      record_error(error, "generator.tones must not be empty");
      return std::nullopt;
    }
    bool all_valid = true;
    std::vector<Tone> tones;
    tones.reserve(entries->size());
    std::transform(entries->begin(), entries->end(), std::back_inserter(tones),
                   [&all_valid, error](const ConfigValue &entry) {
                     Tone tone{};
                     if (!parse_tone(entry, tone, error)) {
                       all_valid = false;
                     }
                     return tone;
                   });
    if (!all_valid) {
      return std::nullopt;
    }
    return GeneratorSpec::multitone(MultitoneParameters{amplitude, std::move(tones)});
  }
  case GeneratorType::impulse: {
    std::size_t position = 0;
    if (!require_position(data, position, error)) {
      return std::nullopt;
    }
    return GeneratorSpec::impulse(ImpulseParameters{amplitude, position});
  }
  }
  return std::nullopt;
}

// Sample rate and frame count are intentionally distinct, named units at this API boundary.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<double> generate_samples(const GeneratorSpec &spec, double sample_rate_hz,
                                     std::size_t frames) {
  std::vector<double> samples;
  if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
    return samples;
  }
  samples.resize(frames, 0.0);
  constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;

  if (spec.type() == GeneratorType::sine) {
    const SineParameters &sine = std::get<SineParameters>(spec.parameters());
    for (std::size_t n = 0; n < frames; ++n) {
      const double phase =
          kTwoPi * sine.frequency_hz * static_cast<double>(n) / sample_rate_hz + sine.phase_rad;
      samples[n] = sine.amplitude * std::sin(phase);
    }
  } else if (spec.type() == GeneratorType::multitone) {
    const MultitoneParameters &multitone = std::get<MultitoneParameters>(spec.parameters());
    for (std::size_t n = 0; n < frames; ++n) {
      double sum = 0.0;
      for (const Tone &tone : multitone.tones) {
        const double phase =
            kTwoPi * tone.frequency_hz * static_cast<double>(n) / sample_rate_hz + tone.phase_rad;
        sum += tone.weight * std::sin(phase);
      }
      samples[n] = multitone.amplitude * sum;
    }
  } else if (spec.type() == GeneratorType::impulse) {
    const ImpulseParameters &impulse = std::get<ImpulseParameters>(spec.parameters());
    if (impulse.position < frames) {
      samples[impulse.position] = impulse.amplitude;
    }
  }
  return samples;
}

} // namespace ztick
