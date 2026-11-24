// SPDX-License-Identifier: MIT
/**
 * @file frontend_c11_test.c
 * @brief Standalone C11 caller check for the private front-end C boundary.
 *
 * Unlike the Rust smoke test, this translation unit is compiled as C11 and
 * links the facade directly. It exercises every exported function, checks
 * copied string and event ownership, and verifies the documented error paths.
 */

#include <ztick/frontend.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/** @brief Complete, runnable v1 config document used by the parse and save checks. */
static const char kConfigJson[] =
    "{\"config_version\":\"1.0.0\",\"sdk_version\":\"1.0.0\","
    "\"integration\":{\"id\":\"app-a\",\"version\":\"1.2.3\"},"
    "\"workload\":{\"level\":\"processor\",\"processors\":[\"eq\"],"
    "\"generator\":{\"kind\":\"sine\",\"amplitude\":0.5,\"frequency_hz\":1000.0,"
    "\"phase_rad\":0.0}},"
    "\"dimensions\":{\"sample_rate_hz\":48000.0,\"block_size\":128,\"channels\":2,"
    "\"oversampling\":1,\"instances\":1},"
    "\"execution\":{\"duration_mode\":\"iterations\",\"iterations\":100,"
    "\"duration_seconds\":0.0,\"repetitions\":2,\"warmup\":{\"iterations\":0},"
    "\"isolation\":\"auto\"},"
    "\"profile\":{\"name\":\"Quick\"},"
    "\"measurement\":{\"detail\":\"aggregate\",\"end_to_end\":false,"
    "\"timing_levels\":[\"per_block\",\"whole_run\"]},"
    "\"validation\":{\"level\":\"Basic\",\"tolerances\":{},\"references\":[]},"
    "\"parallelism\":{\"workers\":\"auto\",\"affinity\":\"auto\"}}";

static const char kInvalidConfigJson[] =
    "{\"config_version\":\"1.0.0\",\"sdk_version\":\"1.0.0\","
    "\"integration\":{\"id\":\"app-a\",\"version\":\"1.2.3\"},"
    "\"workload\":{\"level\":\"processor\",\"processors\":[\"eq\"],"
    "\"generator\":{\"kind\":\"sine\",\"amplitude\":0.5,\"frequency_hz\":1000.0,"
    "\"phase_rad\":0.0}},"
    "\"dimensions\":{\"sample_rate_hz\":48000.0,\"block_size\":128,\"channels\":2,"
    "\"oversampling\":1,\"instances\":1},"
    "\"execution\":{\"duration_mode\":\"iterations\",\"iterations\":0,"
    "\"duration_seconds\":0.0,\"repetitions\":2,\"warmup\":{\"iterations\":0},"
    "\"isolation\":\"auto\"},"
    "\"profile\":{\"name\":\"Quick\"},"
    "\"measurement\":{\"detail\":\"aggregate\",\"end_to_end\":false,"
    "\"timing_levels\":[\"per_block\",\"whole_run\"]},"
    "\"validation\":{\"level\":\"Basic\",\"tolerances\":{},\"references\":[]},"
    "\"parallelism\":{\"workers\":\"auto\",\"affinity\":\"auto\"}}";

/** @brief Number of progress callbacks observed by this test. */
static int g_callback_count = 0;

/**
 * @brief Compare one condition with a numbered failure code.
 * @param condition Condition expected to be true.
 * @param failure_code Code returned when @p condition is false.
 * @return Zero on success, otherwise @p failure_code.
 */
static int require(int condition, int failure_code) { return condition ? 0 : failure_code; }

/**
 * @brief Receive one copied progress event and count it.
 * @param event Borrowed event valid for the callback duration.
 * @param user_data Unused callback context.
 */
static void count_event(const ztick_frontend_event *event, void *user_data) {
  (void)event;
  (void)user_data;
  ++g_callback_count;
}

/**
 * @brief Build one unique temporary path under /tmp.
 * @param label Artifact label used as the filename suffix.
 * @param buffer Destination path buffer.
 * @param capacity Byte capacity of @p buffer.
 * @return Zero on success, otherwise one when @p buffer is too small.
 */
static int temp_path(const char *label, char *buffer, size_t capacity) {
  const int written =
      snprintf(buffer, capacity, "/tmp/ztick_frontend_c11_%d_%s", (int)getpid(), label);
  return written > 0 && (size_t)written < capacity ? 0 : 1;
}

/**
 * @brief Exercise malformed and NULL arguments on every boundary function.
 * @return Zero when every call returns its documented status, otherwise a call index.
 */
static int check_error_paths(void) {
  char buffer[256] = {0};
  size_t length = 0;
  int32_t trusted = 0;
  ztick_frontend_event event = {0};
  event.size = sizeof(event);
  event.version = ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION;

  if (ztick_frontend_get_version(NULL) != ztick_status_invalid_argument) {
    return 1;
  }
  if (strcmp(ztick_frontend_status_message(ztick_status_invalid_argument), "invalid argument") !=
      0) {
    return 2;
  }
  ztick_frontend_session_options malformed = {0};
  malformed.size = 1U;
  malformed.version = ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION;
  if (ztick_frontend_session_create(&malformed) != NULL) {
    return 3;
  }
  if (ztick_frontend_last_error(NULL, buffer, sizeof(buffer), &length) !=
      ztick_status_invalid_argument) {
    return 4;
  }
  if (ztick_frontend_config_parse(NULL, kConfigJson, sizeof(kConfigJson) - 1U) !=
      ztick_status_invalid_argument) {
    return 5;
  }
  if (ztick_frontend_config_load(NULL, "missing.json") != ztick_status_invalid_argument) {
    return 6;
  }
  if (ztick_frontend_config_validate(NULL) != ztick_status_invalid_argument) {
    return 7;
  }
  if (ztick_frontend_config_override(NULL, NULL, 0U, buffer, sizeof(buffer), &length) !=
      ztick_status_invalid_argument) {
    return 23;
  }
  if (ztick_frontend_config_serialize(NULL, buffer, sizeof(buffer), &length) !=
      ztick_status_invalid_argument) {
    return 8;
  }
  if (ztick_frontend_config_save(NULL, "missing.json") != ztick_status_invalid_argument) {
    return 9;
  }
  if (ztick_frontend_list(NULL, buffer, sizeof(buffer), &length) != ztick_status_invalid_argument) {
    return 10;
  }
  if (ztick_frontend_system(NULL, buffer, sizeof(buffer), &length) !=
      ztick_status_invalid_argument) {
    return 11;
  }
  if (ztick_frontend_run(NULL, "missing.json", buffer, sizeof(buffer), &length) !=
      ztick_status_invalid_argument) {
    return 12;
  }
  if (ztick_frontend_report(NULL, "missing.json", buffer, sizeof(buffer), &length) !=
      ztick_status_invalid_argument) {
    return 13;
  }
  if (ztick_frontend_compare(NULL, "a.json", "b.json", buffer, sizeof(buffer), &length) !=
      ztick_status_invalid_argument) {
    return 14;
  }
  if (ztick_frontend_result_read(NULL, "missing.json", buffer, sizeof(buffer), &length) !=
      ztick_status_invalid_argument) {
    return 15;
  }
  if (ztick_frontend_result_export_csv(NULL, "missing.json", NULL, buffer, sizeof(buffer),
                                       &length) != ztick_status_invalid_argument) {
    return 24;
  }
  if (ztick_frontend_plugin_list(NULL, buffer, sizeof(buffer), &length) !=
      ztick_status_invalid_argument) {
    return 16;
  }
  if (ztick_frontend_plugin_inspect(NULL, "missing.so", buffer, sizeof(buffer), &length) !=
      ztick_status_invalid_argument) {
    return 17;
  }
  if (ztick_frontend_trust_query(NULL, "id", &trusted) != ztick_status_invalid_argument) {
    return 18;
  }
  if (ztick_frontend_trust_consent(NULL, "id", 1) != ztick_status_invalid_argument) {
    return 19;
  }
  if (ztick_frontend_progress_subscribe(NULL, count_event, NULL) != ztick_status_invalid_argument) {
    return 20;
  }
  if (ztick_frontend_progress_poll(NULL, &event, &trusted) != ztick_status_invalid_argument) {
    return 21;
  }
  if (ztick_frontend_progress_drain(NULL, count_event, NULL, &trusted) !=
      ztick_status_invalid_argument) {
    return 22;
  }
  return 0;
}

/**
 * @brief Entry point for the C11 front-end boundary check.
 * @return Zero when every call returns its expected status, otherwise a call index.
 */
int main(void) {
  char config_path[256] = {0};
  char result_path[256] = {0};
  char trust_path[256] = {0};
  char missing_path[256] = {0};
  char output[131072] = {0};
  size_t length = 0;
  int32_t available = 0;
  int32_t drained = 0;
  int32_t trusted = 1;
  int check = 0;

  if ((check = check_error_paths()) != 0) {
    return check;
  }
  if ((check = temp_path("config.json", config_path, sizeof(config_path))) != 0) {
    return 100 + check;
  }
  if ((check = temp_path("config.result.json", result_path, sizeof(result_path))) != 0) {
    return 130 + check;
  }
  if ((check = temp_path("trust.list", trust_path, sizeof(trust_path))) != 0) {
    return 110 + check;
  }
  if ((check = temp_path("missing.json", missing_path, sizeof(missing_path))) != 0) {
    return 120 + check;
  }

  ztick_frontend_version version = {0};
  version.size = sizeof(version);
  version.version = ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION;
  if ((check = require(ztick_frontend_get_version(&version) == ztick_status_success, 1)) != 0) {
    return check;
  }
  if ((check = require(version.major == ZTICK_FRONTEND_VERSION_MAJOR &&
                           version.minor == ZTICK_FRONTEND_VERSION_MINOR &&
                           version.patch == ZTICK_FRONTEND_VERSION_PATCH,
                       2)) != 0) {
    return check;
  }

  ztick_frontend_session_options options = {0};
  options.size = sizeof(options);
  options.version = ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION;
  options.trust_store_path = trust_path;
  ztick_frontend_session *session = ztick_frontend_session_create(&options);
  if (session == NULL) {
    return 3;
  }
  if ((check = require(ztick_frontend_last_error(session, output, sizeof(output), &length) ==
                               ztick_status_success &&
                           length == 0U && output[0] == '\0',
                       4)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_progress_subscribe(session, count_event, NULL) ==
                           ztick_status_success,
                       5)) != 0) {
    return check;
  }

  if ((check = require(ztick_frontend_config_parse(
                           session, kConfigJson, sizeof(kConfigJson) - 1U) == ztick_status_success,
                       6)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_config_validate(session) == ztick_status_success, 7)) != 0) {
    return check;
  }
  {
    char retained[sizeof(output)];
    size_t retained_length = 0U;
    if ((check = require(
             ztick_frontend_config_serialize(session, retained, sizeof(retained),
                                             &retained_length) == ztick_status_success,
             230)) != 0) {
      return check;
    }
    if ((check = require(ztick_frontend_config_parse(session, kInvalidConfigJson,
                                                     sizeof(kInvalidConfigJson) - 1U) ==
                                 ztick_status_validation_failure,
                         231)) != 0) {
      return check;
    }
    if ((check = require(
             ztick_frontend_config_serialize(session, output, sizeof(output), &length) ==
                 ztick_status_success &&
             strcmp(output, retained) == 0,
             232)) != 0) {
      return check;
    }
  }
  {
    const char *overrides[] = {"dimensions.channels=4"};
    if ((check =
             require(ztick_frontend_config_override(session, overrides, 1U, output, sizeof(output),
                                                    &length) == ztick_status_success &&
                         strstr(output, "\"channels\":4") != NULL,
                     200)) != 0) {
      return check;
    }
  }
  {
    const char *invalid_override[] = {"dimensions.channels=0"};
    if ((check = require(ztick_frontend_config_override(session, invalid_override, 1U, output,
                                                        sizeof(output), &length) ==
                                 ztick_status_validation_failure,
                             201)) != 0) {
      return check;
    }
    if ((check = require(ztick_frontend_config_validate(session) == ztick_status_success,
                         202)) != 0) {
      return check;
    }
    if ((check =
             require(ztick_frontend_config_serialize(session, output, sizeof(output), &length) ==
                             ztick_status_success &&
                         strstr(output, "\"channels\":4") != NULL,
                     203)) != 0) {
      return check;
    }
  }
  if ((check = require(ztick_frontend_config_serialize(session, output, 1U, &length) ==
                           ztick_status_invalid_argument,
                       8)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_last_error(session, output, sizeof(output), &length) ==
                               ztick_status_success &&
                           length > 0U && strstr(output, "buffer") != NULL,
                       27)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_config_serialize(session, output, sizeof(output), &length) ==
                               ztick_status_success &&
                           strstr(output, "config_version") != NULL,
                       9)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_config_save(session, config_path) == ztick_status_success,
                       10)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_config_load(session, config_path) == ztick_status_success,
                       11)) != 0) {
    return check;
  }
  {
    char invalid_path[256] = {0};
    FILE *file = NULL;
    if ((check = temp_path("invalid-config.json", invalid_path, sizeof(invalid_path))) != 0) {
      return check;
    }
    file = fopen(invalid_path, "wb");
    if (file == NULL) {
      return 12;
    }
    if (fwrite(kInvalidConfigJson, 1U, sizeof(kInvalidConfigJson) - 1U, file) !=
        sizeof(kInvalidConfigJson) - 1U) {
      fclose(file);
      return 13;
    }
    fclose(file);
    if ((check = require(ztick_frontend_config_load(session, invalid_path) ==
                                 ztick_status_validation_failure,
                         233)) != 0) {
      return check;
    }
    (void)remove(invalid_path);
  }

  if ((check = require(ztick_frontend_list(session, output, sizeof(output), &length) ==
                               ztick_status_success &&
                           strstr(output, "data") != NULL,
                       12)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_system(session, output, sizeof(output), &length) ==
                           ztick_status_success,
                       13)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_plugin_list(session, output, sizeof(output), &length) ==
                           ztick_status_success,
                       14)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_run(session, missing_path, output, sizeof(output), &length) !=
                           ztick_status_success,
                       15)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_report(session, missing_path, output, sizeof(output),
                                             &length) != ztick_status_success,
                       16)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_compare(session, missing_path, missing_path, output,
                                              sizeof(output), &length) != ztick_status_success,
                       17)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_result_read(session, missing_path, output, sizeof(output),
                                                  &length) != ztick_status_success,
                       18)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_plugin_inspect(session, missing_path, output, sizeof(output),
                                                     &length) != ztick_status_success,
                       19)) != 0) {
    return check;
  }

  if ((check = require(ztick_frontend_run(session, config_path, output, sizeof(output), &length) ==
                           ztick_status_success,
                       201)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_report(session, result_path, output, sizeof(output),
                                             &length) == ztick_status_success &&
                           strstr(output, "data") != NULL,
                       202)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_compare(session, result_path, result_path, output,
                                              sizeof(output), &length) == ztick_status_success,
                       203)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_result_read(session, result_path, output, sizeof(output),
                                                  &length) == ztick_status_success &&
                           strstr(output, "config_version") != NULL,
                       204)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_result_export_csv(session, result_path, "per_test_point",
                                                        output, sizeof(output),
                                                        &length) == ztick_status_success &&
                           strstr(output, "session_id") != NULL,
                       205)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_result_export_csv(session, result_path, "unknown-mode",
                                                        output, sizeof(output),
                                                        &length) == ztick_status_invalid_argument,
                       206)) != 0) {
    return check;
  }

  if ((check = require(ztick_frontend_trust_query(session, "c11.smoke.plugin", &trusted) ==
                               ztick_status_success &&
                           trusted == 0,
                       20)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_trust_consent(session, "c11.smoke.plugin", 1) ==
                           ztick_status_success,
                       21)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_trust_query(session, "c11.smoke.plugin", &trusted) ==
                               ztick_status_success &&
                           trusted == 1,
                       22)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_trust_consent(session, "c11.smoke.plugin", 0) ==
                           ztick_status_success,
                       23)) != 0) {
    return check;
  }

  ztick_frontend_event event = {0};
  event.size = sizeof(event);
  event.version = ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION;
  if ((check = require(ztick_frontend_progress_poll(session, &event, &available) ==
                               ztick_status_success &&
                           available == 1 && event.kind >= ztick_frontend_event_state_change &&
                           event.timing_level == ztick_frontend_timing_unknown &&
                           event.isolation_mode == ztick_frontend_isolation_unknown,
                       24)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_progress_drain(session, count_event, NULL, &drained) ==
                               ztick_status_success &&
                           drained > 0 && g_callback_count > 0,
                       25)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_progress_poll(session, &event, &available) ==
                               ztick_status_success &&
                           available == 0,
                       26)) != 0) {
    return check;
  }

  ztick_frontend_session_destroy(session);
  if ((check = require(ztick_frontend_config_validate(session) ==
                           ztick_status_invalid_argument,
                       28)) != 0) {
    return check;
  }
  ztick_frontend_session_destroy(session);

  ztick_frontend_session_options replacement_options = {0};
  replacement_options.size = sizeof(replacement_options);
  replacement_options.version = ZTICK_FRONTEND_STRUCT_LAYOUT_VERSION;
  ztick_frontend_session *replacement = ztick_frontend_session_create(&replacement_options);
  if ((check = require(replacement != NULL && replacement != session, 29)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_config_validate(session) ==
                           ztick_status_invalid_argument,
                       30)) != 0) {
    return check;
  }
  if ((check = require(ztick_frontend_config_validate(replacement) ==
                           ztick_status_invalid_argument,
                       31)) != 0) {
    return check;
  }
  ztick_frontend_session_destroy(replacement);
  (void)remove(config_path);
  (void)remove(result_path);
  (void)remove(trust_path);
  return 0;
}
