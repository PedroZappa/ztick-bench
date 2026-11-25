// SPDX-License-Identifier: MIT
/**
 * @file metadata_test.cpp
 * @brief Catch2 tests for metadata attachment and comparability warnings.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/config_document.hpp>
#include <ztick/metadata.hpp>
#include <ztick/result_document.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

/**
 * @brief Build the complete v1 config required by a result document.
 * @return Valid config JSON text.
 */
std::string make_config() {
  std::string text = R"json({"config_version":"1.0.0","sdk_version":"1.0.0",)json";
  text.append(R"json("integration":{"id":"app-a","version":"1.2.3"},)json");
  text.append(R"json("workload":{"level":"processor","processors":["eq"],)json");
  text.append(R"json("generator":{"type":"noise"}},)json");
  text.append(R"json("dimensions":{"sample_rate_hz":48000,"block_size":128,)json");
  text.append(R"json("channels":2,"oversampling":1,"instances":1},)json");
  text.append(R"json("execution":{"duration_mode":"iterations","iterations":1000,)json");
  text.append(R"json("repetitions":3,"warmup":{"iterations":100},"isolation":"auto"},)json");
  text.append(R"json("profile":{"name":"Quick"},)json");
  text.append(R"json("measurement":{"detail":"aggregate","end_to_end":false,)json");
  text.append(R"json("timing_levels":["per_block","whole_run"]},)json");
  text.append(R"json("validation":{"level":"Basic","tolerances":{},"references":[]},)json");
  text.append(R"json("parallelism":{"workers":"auto","affinity":"auto"}})json");
  return text;
}

/**
 * @brief Build a minimal valid result document.
 * @return Result-shaped ConfigValue with empty metadata sections.
 */
ztick::ConfigValue make_document() {
  ztick::ConfigValue document = ztick::ConfigValue::object();
  document.set("schema_version",
               ztick::ConfigValue::string(std::string(ztick::kResultSchemaVersion)));
  document.set("sdk_version", ztick::ConfigValue::string("1.0.0"));
  ztick::ConfigValue session = ztick::ConfigValue::object();
  session.set("id", ztick::ConfigValue::string("session-1"));
  document.set("session", std::move(session));
  document.set("config_snapshot", ztick::parse_config_document(make_config()).document);
  document.set("build", ztick::ConfigValue::object());
  document.set("machine", ztick::ConfigValue::object());
  document.set("environment", ztick::ConfigValue::object());
  document.set("runs", ztick::ConfigValue::array());
  document.set("comparisons", ztick::ConfigValue::array());
  document.set("warnings", ztick::ConfigValue::array());
  document.set("failures", ztick::ConfigValue::array());
  document.set("status", ztick::ConfigValue::string("complete"));
  return document;
}

/**
 * @brief Read one metadata record member as text.
 * @param section Metadata object holding the record.
 * @param field Record name.
 * @param member Member to read: `status`, `value` or `reason`.
 * @return Stored text, or an empty string when absent.
 */
std::string record_text(const ztick::ConfigValue *section, std::string_view field,
                        std::string_view member) {
  const ztick::ConfigValue *record = section == nullptr ? nullptr : section->find(field);
  if (record == nullptr) {
    return {};
  }
  const ztick::ConfigValue *text = record->find(member);
  return text == nullptr || text->type() != ztick::ConfigValueType::string ? std::string{}
                                                                           : *(text->as_string());
}

/**
 * @brief Return a dynamically generated result path.
 * @param name Short discriminator for the calling test case.
 * @return Path inside the system temp directory.
 */
std::filesystem::path result_path(std::string_view name) {
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("ztick_meta_" + std::string(name) + "_" + std::to_string(ticks) + ".json");
}

/**
 * @brief Remove a generated file without throwing.
 * @param path Path to remove.
 */
void remove_file(const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::remove(path, error);
}

} // namespace

/**
 * @brief Verify metadata status tokens map to and from their stable names.
 */
TEST_CASE("metadata status tokens round trip", "[results][metadata]") {
  using ztick::MetadataStatus;

  REQUIRE(ztick::metadata_status_token(MetadataStatus::value) == "value");
  REQUIRE(ztick::metadata_status_token(MetadataStatus::unavailable) == "unavailable");
  REQUIRE(ztick::metadata_status_token(MetadataStatus::unsupported) == "unsupported");
  REQUIRE(ztick::metadata_status_token(MetadataStatus::not_applicable) == "not_applicable");
  REQUIRE(ztick::parse_metadata_status("value") == MetadataStatus::value);
  REQUIRE(ztick::parse_metadata_status("unavailable") == MetadataStatus::unavailable);
  REQUIRE(ztick::parse_metadata_status("unsupported") == MetadataStatus::unsupported);
  REQUIRE(ztick::parse_metadata_status("not_applicable") == MetadataStatus::not_applicable);
  REQUIRE_FALSE(ztick::parse_metadata_status("measured").has_value());
}

/**
 * @brief Verify field helpers always produce a present, uniform record.
 */
TEST_CASE("metadata fields keep status value and reason", "[results][metadata]") {
  const std::vector<ztick::MetadataField> fields{
      ztick::metadata_value("present", "42"),
      ztick::metadata_unavailable("missing", "probe failed"),
      ztick::metadata_unsupported("unimplemented", "no backend"),
      ztick::metadata_not_applicable("audio", "offline mode"),
  };
  const ztick::ConfigValue object = ztick::make_metadata_object(fields);

  REQUIRE(record_text(&object, "present", "status") == "value");
  REQUIRE(record_text(&object, "present", "value") == "42");
  REQUIRE(record_text(&object, "present", "reason").empty());

  REQUIRE(record_text(&object, "missing", "status") == "unavailable");
  REQUIRE(record_text(&object, "missing", "value").empty());
  REQUIRE(record_text(&object, "missing", "reason") == "probe failed");
  REQUIRE(record_text(&object, "unimplemented", "status") == "unsupported");
  REQUIRE(record_text(&object, "audio", "status") == "not_applicable");
}

/**
 * @brief Verify build metadata records every discovered fact and reports missing ones.
 */
TEST_CASE("build metadata records all facts", "[results][metadata]") {
  const ztick::BuildInfo info{"deadbeef", "GNU", "14.2.0", "-O3 -DNDEBUG", "Release", "0.5.0-dev"};
  const ztick::ConfigValue build = ztick::build_metadata(info);

  REQUIRE(record_text(&build, "git_commit", "value") == "deadbeef");
  REQUIRE(record_text(&build, "compiler", "value") == "GNU");
  REQUIRE(record_text(&build, "compiler_version", "value") == "14.2.0");
  REQUIRE(record_text(&build, "optimization_flags", "value") == "-O3 -DNDEBUG");
  REQUIRE(record_text(&build, "build_configuration", "value") == "Release");
  REQUIRE(record_text(&build, "project_version", "value") == "0.5.0-dev");

  const ztick::BuildInfo missing{"", "", "", "", "", ""};
  const ztick::ConfigValue unavailable = ztick::build_metadata(missing);
  REQUIRE(record_text(&unavailable, "git_commit", "status") == "unavailable");
  REQUIRE_FALSE(record_text(&unavailable, "git_commit", "reason").empty());
  REQUIRE(record_text(&unavailable, "compiler", "status") == "unavailable");
}

/**
 * @brief Verify the no-argument builder uses the compile-time captured facts.
 */
TEST_CASE("build metadata uses current build info", "[results][metadata]") {
  const ztick::BuildInfo info = ztick::current_build_info();
  const ztick::ConfigValue build = ztick::build_metadata();

  REQUIRE_FALSE(info.git_commit.empty());
  REQUIRE(record_text(&build, "git_commit", "value") == info.git_commit);
  REQUIRE(record_text(&build, "compiler", "value") == info.compiler);
  REQUIRE(record_text(&build, "compiler_version", "value") == info.compiler_version);
  REQUIRE(record_text(&build, "build_configuration", "value") == info.build_configuration);
}

/**
 * @brief Verify workload and environment sections are projected from the config.
 */
TEST_CASE("config metadata sections project present fields", "[results][metadata]") {
  const ztick::ConfigValue config = ztick::parse_config_document(make_config()).document;
  const ztick::ConfigValue workload = ztick::workload_metadata(config);
  const ztick::ConfigValue environment = ztick::environment_metadata(config);

  REQUIRE(record_text(&workload, "integration_id", "value") == "app-a");
  REQUIRE(record_text(&workload, "integration_version", "value") == "1.2.3");
  REQUIRE(record_text(&workload, "workload_level", "value") == "processor");
  REQUIRE(record_text(&workload, "block_size", "value") == "128");
  REQUIRE(record_text(&environment, "isolation", "value") == "auto");
  REQUIRE(record_text(&environment, "workers", "value") == "auto");
  REQUIRE(record_text(&environment, "detail", "value") == "aggregate");

  const ztick::ConfigValue empty = ztick::ConfigValue::object();
  const ztick::ConfigValue empty_workload = ztick::workload_metadata(empty);
  const ztick::ConfigValue empty_environment = ztick::environment_metadata(empty);
  REQUIRE(record_text(&empty_workload, "integration_id", "status") == "unavailable");
  REQUIRE(record_text(&empty_environment, "isolation", "status") == "unavailable");
}

/**
 * @brief Verify attachment populates all sections and rejects invalid input atomically.
 */
TEST_CASE("metadata attaches to a result document", "[results][metadata]") {
  ztick::ConfigValue document = make_document();
  const ztick::ConfigValue build = ztick::build_metadata();
  const ztick::ConfigValue machine =
      ztick::make_metadata_object({ztick::metadata_value("cpu_model", "Test CPU")});
  const ztick::ConfigValue environment =
      ztick::environment_metadata(ztick::parse_config_document(make_config()).document);

  std::string error;
  REQUIRE(ztick::attach_metadata(document, build, machine, environment, &error));
  REQUIRE(error.empty());
  REQUIRE(record_text(document.find("build"), "git_commit", "status") == "value");
  REQUIRE(record_text(document.find("machine"), "cpu_model", "value") == "Test CPU");
  REQUIRE(record_text(document.find("environment"), "isolation", "value") == "auto");
  REQUIRE(ztick::validate_result_document(document, &error));

  const ztick::ConfigValue before = document;
  REQUIRE_FALSE(
      ztick::attach_metadata(document, ztick::ConfigValue::array(), machine, environment, &error));
  REQUIRE_FALSE(error.empty());
  REQUIRE(document == before);
  ztick::ConfigValue invalid_document = ztick::ConfigValue::null();
  REQUIRE_FALSE(ztick::attach_metadata(invalid_document, build, machine, environment));
}

/**
 * @brief Verify comparability warnings fire only on genuine metadata differences.
 */
TEST_CASE("comparability warnings fire on metadata mismatch", "[results][metadata]") {
  ztick::ConfigValue baseline = make_document();
  const ztick::ConfigValue build = ztick::build_metadata();
  const ztick::ConfigValue environment = ztick::ConfigValue::object();
  const ztick::ConfigValue machine_a =
      ztick::make_metadata_object({ztick::metadata_value("cpu_model", "CPU A"),
                                   ztick::metadata_value("logical_core_count", "8"),
                                   ztick::metadata_unavailable("kernel_version", "probe failed")});
  REQUIRE(ztick::attach_metadata(baseline, build, machine_a, environment));

  ztick::ConfigValue same = baseline;
  const ztick::MetadataComparison identical = ztick::compare_metadata(baseline, same);
  REQUIRE(identical.comparable);
  REQUIRE(identical.warnings.as_array()->empty());

  ztick::ConfigValue different = baseline;
  const ztick::ConfigValue machine_b =
      ztick::make_metadata_object({ztick::metadata_value("cpu_model", "CPU B"),
                                   ztick::metadata_value("logical_core_count", "8"),
                                   ztick::metadata_unavailable("kernel_version", "probe failed")});
  REQUIRE(ztick::attach_metadata(different, build, machine_b, environment));

  const ztick::MetadataComparison comparison = ztick::compare_metadata(baseline, different);
  REQUIRE_FALSE(comparison.comparable);
  REQUIRE(comparison.difference_count == 1U);
  REQUIRE(comparison.warnings.as_array()->size() == 1U);
  const ztick::ConfigValue &warning = comparison.warnings.as_array()->front();
  REQUIRE((*warning.find("category")->as_string()) == "comparability");
  REQUIRE((*warning.find("field")->as_string()) == "machine.cpu_model");
  REQUIRE((*warning.find("left_value")->as_string()) == "CPU A");
  REQUIRE((*warning.find("right_value")->as_string()) == "CPU B");

  ztick::ConfigValue status_shift = baseline;
  const ztick::ConfigValue machine_c =
      ztick::make_metadata_object({ztick::metadata_unavailable("cpu_model", "probe failed"),
                                   ztick::metadata_value("logical_core_count", "8"),
                                   ztick::metadata_unavailable("kernel_version", "probe failed")});
  REQUIRE(ztick::attach_metadata(status_shift, build, machine_c, environment));
  const ztick::MetadataComparison shifted = ztick::compare_metadata(baseline, status_shift);
  REQUIRE_FALSE(shifted.comparable);
  REQUIRE(shifted.difference_count == 1U);
  REQUIRE((*shifted.warnings.as_array()->front().find("field")->as_string()) ==
          "machine.cpu_model");

  REQUIRE_FALSE(ztick::compare_metadata(ztick::ConfigValue::null(), baseline).comparable);
}

/**
 * @brief Verify attached metadata survives a result document round trip.
 */
TEST_CASE("attached metadata survives result storage", "[results][metadata]") {
  const std::filesystem::path path = result_path("round_trip");
  ztick::ConfigValue document = make_document();
  const ztick::ConfigValue build = ztick::build_metadata();
  const ztick::ConfigValue machine =
      ztick::make_metadata_object({ztick::metadata_value("cpu_model", "Roundtrip CPU")});
  const ztick::ConfigValue environment = ztick::ConfigValue::object();
  REQUIRE(ztick::attach_metadata(document, build, machine, environment));
  REQUIRE(ztick::write_result_document(path, document).valid());

  const ztick::ResultParseResult loaded = ztick::read_result_document(path);
  REQUIRE(loaded.valid());
  REQUIRE(record_text(loaded.document.find("build"), "git_commit", "status") == "value");
  REQUIRE(record_text(loaded.document.find("machine"), "cpu_model", "value") == "Roundtrip CPU");
  REQUIRE(ztick::compare_metadata(document, loaded.document).comparable);

  remove_file(path);
}
