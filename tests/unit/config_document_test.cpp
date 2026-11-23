// SPDX-License-Identifier: MIT
/**
 * @file config_document_test.cpp
 * @brief Catch2 tests for the portable config document schema and parser.
 */

#include <catch2/catch_test_macros.hpp>

#include <ztick/config_document.hpp>

#include <string>
#include <string_view>

namespace {

/**
 * @brief Build a complete v1 config document with explicit version tokens.
 * @param config_version Value of the `config_version` field.
 * @param sdk_version Value of the `sdk_version` field.
 * @return JSON text with every top-level section required by RESULTS_FORMAT §6.
 */
std::string make_config(std::string_view config_version, std::string_view sdk_version) {
  std::string text = "{\"config_version\":\"";
  text.append(config_version);
  text.append("\",\"sdk_version\":\"");
  text.append(sdk_version);
  text.append(R"json(","integration":{"id":"app-a","version":"1.2.3"},)json");
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

} // namespace

/**
 * @brief Verify a complete v1 config parses and exposes its version fields.
 */
TEST_CASE("config document parser accepts a complete v1 document", "[results][config]") {
  const auto result = ztick::parse_config_document(make_config("1.0.0", "1.0.0"));
  REQUIRE(result.valid());
  REQUIRE(result.document.type() == ztick::ConfigValueType::object);
  REQUIRE(*(result.document.find("config_version")->as_string()) == ztick::kConfigSchemaVersion);
  REQUIRE(*(result.document.find("sdk_version")->as_string()) == "1.0.0");
  REQUIRE(*(result.document.find("integration")->find("id")->as_string()) == "app-a");
}

/**
 * @brief Verify a parsed config can be written and read without changing content.
 */
TEST_CASE("config document round trips through JSON", "[results][config]") {
  const auto parsed = ztick::parse_config_document(make_config("1.0.0", "1.0.0"));
  REQUIRE(parsed.valid());

  const auto written = ztick::write_config_document(parsed.document);
  REQUIRE(written.valid());
  REQUIRE(written.text.find("\"config_version\":\"1.0.0\"") != std::string::npos);
  REQUIRE(written.offset == written.text.size());

  const auto reparsed = ztick::parse_config_document(written.text);
  REQUIRE(reparsed.valid());
  REQUIRE(reparsed.document == parsed.document);
}

/**
 * @brief Verify a missing config_version makes the whole document invalid.
 */
TEST_CASE("config document requires a version field", "[results][config]") {
  std::string text = make_config("1.0.0", "1.0.0");
  const std::string field = "\"config_version\":\"1.0.0\",";
  const std::size_t position = text.find(field);
  REQUIRE(position != std::string::npos);
  text.erase(position, field.size());

  const auto result = ztick::parse_config_document(text);
  REQUIRE_FALSE(result.valid());
  REQUIRE(result.error.find("config_version") != std::string::npos);
  REQUIRE(result.document.type() == ztick::ConfigValueType::null);
}

/**
 * @brief Verify unsupported config schema and SDK versions are rejected.
 */
TEST_CASE("config document rejects unsupported versions", "[results][config]") {
  const auto future_schema = ztick::parse_config_document(make_config("2.0.0", "1.0.0"));
  REQUIRE_FALSE(future_schema.valid());
  REQUIRE(future_schema.error.find("config_version") != std::string::npos);

  const auto future_sdk = ztick::parse_config_document(make_config("1.0.0", "1.1.0"));
  REQUIRE_FALSE(future_sdk.valid());
  REQUIRE(future_sdk.error.find("sdk_version") != std::string::npos);

  const auto compatible_patch = ztick::parse_config_document(make_config("1.0.0", "1.0.9"));
  REQUIRE(compatible_patch.valid());
}

/**
 * @brief Verify mistyped top-level sections are rejected with a useful error.
 */
TEST_CASE("config document checks section types", "[results][config]") {
  std::string text = make_config("1.0.0", "1.0.0");
  const std::string from = R"("integration":{"id":"app-a","version":"1.2.3"})";
  const std::string to = R"("integration":[])";
  const std::size_t position = text.find(from);
  REQUIRE(position != std::string::npos);
  text.replace(position, from.size(), to);

  const auto result = ztick::parse_config_document(text);
  REQUIRE_FALSE(result.valid());
  REQUIRE(result.error.find("integration must be object") != std::string::npos);
}

/**
 * @brief Verify version helpers cover valid, malformed and incompatible tokens.
 */
TEST_CASE("semantic version helpers enforce SDK compatibility", "[results][config]") {
  using ztick::SemanticVersion;

  const auto parsed = ztick::parse_semantic_version("1.2.3");
  REQUIRE(parsed.has_value());
  REQUIRE(*parsed == SemanticVersion{1, 2, 3});

  REQUIRE_FALSE(ztick::parse_semantic_version("1.2").has_value());
  REQUIRE_FALSE(ztick::parse_semantic_version("1.2.3-rc").has_value());
  REQUIRE_FALSE(ztick::parse_semantic_version("").has_value());

  REQUIRE(ztick::sdk_version_supported(SemanticVersion{1, 0, 0}));
  REQUIRE(ztick::sdk_version_supported(SemanticVersion{1, 0, 7}));
  REQUIRE_FALSE(ztick::sdk_version_supported(SemanticVersion{2, 0, 0}));
  REQUIRE_FALSE(ztick::sdk_version_supported(SemanticVersion{1, 1, 0}));
}

/**
 * @brief Verify writers and validators reject structurally incomplete documents.
 */
TEST_CASE("config document writer validates before encoding", "[results][config]") {
  std::string error;
  REQUIRE_FALSE(ztick::validate_config_document(ztick::ConfigValue::null(), &error));
  REQUIRE_FALSE(error.empty());

  ztick::ConfigValue partial = ztick::ConfigValue::object();
  REQUIRE(partial.set("config_version", ztick::ConfigValue::string("1.0.0")));
  const auto written = ztick::write_config_document(partial);
  REQUIRE_FALSE(written.valid());
  REQUIRE_FALSE(written.error.empty());
  REQUIRE(written.text.empty());
}
