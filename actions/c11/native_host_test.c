#include "native_host.h"
#include <tinytest.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void stateless_leaves(void) {
  praktor_c11_native_host host = {0};
  praktor_c11_bt_host hooks = {0};
  praktor_c11_value_store *values;
  praktor_c11_leaf_parameter params[2] = {
      {"key", PRAKTOR_C11_LEAF_STRING, "answer", 0.0, false},
      {"value", PRAKTOR_C11_LEAF_STRING, "{source}", 0.0, false}};
  praktor_c11_leaf_request request = {0u, "SetVariable", params, 2u, false, NULL};
  salts_bt_node_status status = SALTS_BT_FAILURE;
  char text[65]; size_t size = 0u;
  check(praktor_c11_native_host_init(&host, 2u, 32u, 64u) == SALTS_OK);
  check(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
  values = praktor_c11_native_host_values(&host);
  check(praktor_c11_value_store_set(values, "source", "value") == PRAKTOR_C11_VALUE_OK);
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  check(praktor_c11_value_store_get(values, "answer", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK);
  check(strcmp(text, "value") == 0);
  request.in_parallel_branch = true;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOTSUP);
  request.in_parallel_branch = false;
  params[1].kind = PRAKTOR_C11_LEAF_NUMBER; params[1].number = NAN;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  check(praktor_c11_value_store_get(values, "answer", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK && strcmp(text, "value") == 0);
  params[1].number = 7.0;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK);
  check(praktor_c11_value_store_get(values, "answer", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK && strcmp(text, "7.000000") == 0);
  params[0].text = "overflow";
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOBUFS);
  check(praktor_c11_value_store_size(values) == 2u);
  request.action = "CheckExitCode";
  params[0].name = "input_key"; params[0].text = "answer";
  params[1].name = "expected"; params[1].number = 7.0;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  params[1].number = 7.25;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  params[1].kind = PRAKTOR_C11_LEAF_BOOLEAN;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  params[1].name = NULL;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  request.parameter_count = 0u; request.action = "Unregistered";
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOTSUP);
  praktor_c11_native_host_destroy(&host);
  check(host.impl == NULL);
}

static uint64_t fake_now(void *user) { return *(uint64_t *)user; }

static void sleep_leaves(void) {
  praktor_c11_native_host host = {0};
  praktor_c11_bt_host hooks = {0};
  uint64_t now = 0u;
  int owner_a = 0, owner_b = 0;
  praktor_c11_native_host_config config = {4u, 64u, 128u, 1u, fake_now, &now};
  praktor_c11_leaf_parameter duration = {"duration", PRAKTOR_C11_LEAF_STRING, "2s", 0.0, false};
  praktor_c11_leaf_request request = {0u, "Sleep", &duration, 1u, false, &owner_a};
  praktor_c11_leaf_request other = request;
  salts_bt_node_status status = SALTS_BT_FAILURE;
  const char *invalid[] = {"", "-1ms", "nan", "inf", "1fortnight", "1ms ", "2147483648", "1e999"};
  const struct { const char *text; uint64_t ns; } supported[] = {
      {"1ms", UINT64_C(1000000)}, {"0.001s", UINT64_C(1000000)},
      {"0.01m", UINT64_C(600000000)}, {"0.001h", UINT64_C(3600000000)},
      {" 1.9", UINT64_C(1000000)}, {"2147483647ms", UINT64_C(2147483647000000)}};
  size_t i;
  check(praktor_c11_native_host_init_with_config(&host, &config) == SALTS_OK);
  check(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
  check(hooks.halt_leaf == NULL && hooks.halt_typed_leaf != NULL);
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_RUNNING);
  now = UINT64_C(1999000000);
  duration.text = "0ms";
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_RUNNING);
  now = UINT64_C(2000000000);
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  for (i = 0u; i < sizeof(supported) / sizeof(supported[0]); ++i) {
    now = 0u; duration.text = supported[i].text;
    check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_RUNNING);
    now = supported[i].ns - 1u;
    check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_RUNNING);
    ++now;
    check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  }
  for (i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    duration.text = invalid[i];
    check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  }
  duration.kind = PRAKTOR_C11_LEAF_NUMBER; duration.number = NAN;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  duration.number = -1.0;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  duration.number = 0.9;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  duration.kind = PRAKTOR_C11_LEAF_BOOLEAN;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  duration.kind = PRAKTOR_C11_LEAF_STRING; duration.text = "1s";
  now = 100u;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_RUNNING);
  other.session_owner = &owner_b;
  check(hooks.tick_typed_leaf(hooks.user, &other, &status) == SALTS_ENOBUFS);
  hooks.halt_typed_leaf(hooks.user, &other);
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_RUNNING);
  now = 99u;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  hooks.halt_typed_leaf(hooks.user, &request);
  check(hooks.tick_typed_leaf(hooks.user, &other, &status) == SALTS_OK && status == SALTS_BT_RUNNING);
  hooks.halt_typed_leaf(hooks.user, &other);
  request.session_owner = NULL;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  request.session_owner = &owner_a; request.in_parallel_branch = true;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOTSUP);
  praktor_c11_native_host_destroy(&host);
  config.max_running_leaves = 0u;
  check(praktor_c11_native_host_init_with_config(&host, &config) == SALTS_OK);
  check(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
  request.in_parallel_branch = false;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOBUFS);
  duration.text = "0ms";
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  praktor_c11_native_host_destroy(&host);
}

static void parse_lines(void) {
  praktor_c11_native_host host = {0};
  praktor_c11_bt_host hooks = {0};
  praktor_c11_value_store *values;
  praktor_c11_leaf_parameter params[3] = {
      {"input_key", PRAKTOR_C11_LEAF_STRING, "{source_key}", 0.0, false},
      {"output_key", PRAKTOR_C11_LEAF_STRING, "{destination}", 0.0, false},
      {"filter", PRAKTOR_C11_LEAF_STRING, "{filter}", 0.0, false}};
  praktor_c11_leaf_request request = {0u, "ParseLines", params, 3u, false, NULL};
  salts_bt_node_status status = SALTS_BT_FAILURE;
  char text[129]; size_t size = 0u, i;
  const struct { const char *input; const char *output; const char *count; } cases[] = {
      {"", "", "0"}, {"\n", "", "1"}, {"\n\n", "", "2"},
      {"one\n", "one", "1"}, {"one\n\n", "one\n", "2"},
      {"\n\none\n\ntwo\n", "one\n\ntwo", "5"},
      {"one\r\ntwo\r\n", "one\r\ntwo\r", "2"},
      {"  \n\t\n\xc3\xa9", "  \n\t\n\xc3\xa9", "3"}};
  check(praktor_c11_native_host_init(&host, 8u, 64u, 128u) == SALTS_OK);
  check(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
  values = praktor_c11_native_host_values(&host);
  check(praktor_c11_value_store_set(values, "source_key", "source") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(values, "destination", "lines") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(values, "filter", "") == PRAKTOR_C11_VALUE_OK);
  for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    check(praktor_c11_value_store_set(values, "source", cases[i].input) == PRAKTOR_C11_VALUE_OK);
    check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
    check(praktor_c11_value_store_get(values, "lines", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK && strcmp(text, cases[i].output) == 0);
    check(praktor_c11_value_store_get(values, "lines_count", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK && strcmp(text, cases[i].count) == 0);
  }
  /* The output may replace its own input; parsing used a private snapshot. */
  check(praktor_c11_value_store_set(values, "destination", "source") == PRAKTOR_C11_VALUE_OK);
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  check(praktor_c11_value_store_get(values, "source_count", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK && strcmp(text, "3") == 0);
  check(praktor_c11_value_store_set(values, "filter", "^one$") == PRAKTOR_C11_VALUE_OK);
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOTSUP);
  check(praktor_c11_value_store_set(values, "filter", "[") == PRAKTOR_C11_VALUE_OK);
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOTSUP);
  request.in_parallel_branch = true;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOTSUP);
  request.in_parallel_branch = false;
  check(praktor_c11_value_store_set(values, "filter", "") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_erase(values, "source") == PRAKTOR_C11_VALUE_OK);
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_FAILURE);
  check(praktor_c11_value_store_get(values, "source_count", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK && strcmp(text, "3") == 0);
  praktor_c11_native_host_destroy(&host);
}

static void parse_lines_atomic_failure(void) {
  praktor_c11_native_host host = {0};
  praktor_c11_bt_host hooks = {0};
  praktor_c11_value_store *values;
  praktor_c11_leaf_parameter output = {"output_key", PRAKTOR_C11_LEAF_STRING, "lines", 0.0, false};
  praktor_c11_leaf_request request = {0u, "ParseLines", &output, 1u, false, NULL};
  salts_bt_node_status status = SALTS_BT_FAILURE;
  char text[17]; size_t size = 0u;
  check(praktor_c11_native_host_init(&host, 3u, 12u, 16u) == SALTS_OK);
  check(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
  values = praktor_c11_native_host_values(&host);
  check(praktor_c11_value_store_set(values, "shell_output", "one\ntwo") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(values, "lines", "old") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(values, "full", "occupied") == PRAKTOR_C11_VALUE_OK);
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOBUFS);
  check(praktor_c11_value_store_get(values, "lines", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK && strcmp(text, "old") == 0);
  check(praktor_c11_value_store_erase(values, "full") == PRAKTOR_C11_VALUE_OK);
  output.text = "toolong"; /* derived count key is 13 bytes, key bound is 12 */
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOBUFS);
  check(praktor_c11_value_store_size(values) == 2u);
  output.text = "";
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  output.text = "lines";
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  check(praktor_c11_value_store_get(values, "lines", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK && strcmp(text, "one\ntwo") == 0);
  check(praktor_c11_value_store_get(values, "lines_count", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK && strcmp(text, "2") == 0);
  praktor_c11_native_host_destroy(&host);
  /* Parameter scratch follows the larger key bound; actual values still
   * respect the smaller value bound. */
  check(praktor_c11_native_host_init(&host, 3u, 32u, 1u) == SALTS_OK);
  check(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
  values = praktor_c11_native_host_values(&host);
  check(praktor_c11_value_store_set(values, "shell_output", "x") == PRAKTOR_C11_VALUE_OK);
  request.parameter_count = 0u;
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  check(praktor_c11_value_store_get(values, "parsed_lines_count", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK && strcmp(text, "1") == 0);
  praktor_c11_native_host_destroy(&host);
}

static void parse_key_value(void) {
  praktor_c11_native_host host = {0};
  praktor_c11_bt_host hooks = {0};
  praktor_c11_value_store *values;
  praktor_c11_leaf_parameter params[] = {
      {"input_key", PRAKTOR_C11_LEAF_STRING, "{port}", 0.0, false},
      {"delimiter", PRAKTOR_C11_LEAF_STRING, "{delim}", 0.0, false},
      {"line_separator", PRAKTOR_C11_LEAF_STRING, "{sep}", 0.0, false},
      {"output_key", PRAKTOR_C11_LEAF_STRING, "source", 0.0, false}};
  praktor_c11_leaf_request request = {0u, "ParseKeyValue", params, 4u, false, NULL};
  salts_bt_node_status status = SALTS_BT_FAILURE;
  char text[257]; size_t size = 0u, i;
  const struct { const char *input, *delimiter, *separator, *expected; } cases[] = {
      {"z=first\na=  one \nz=last\nignored\n= empty \na=two=three\n", "=", "\n", "{\"\":\"empty\",\"a\":\"two=three\",\"z\":\"last\"}"},
      {"\t k \r:: v \t||z:: \r||k::last||", "::", "||", "{\"k\":\"last\",\"z\":\"\"}"},
      {"foo\nbar\n", "", "\n", "{\"\":\"bar\"}"},
      {"", "=", "\n", "{}"}, {"ignored\nno delimiter", "=", "\n", "{}"},
      {"\xc3\xa9=\\\"\b\f\t\x01/x\nA=first", "=", "\n", "{\"A\":\"first\",\"\xc3\xa9\":\"\\\\\\\"\\b\\f\\t\\u0001/x\"}"}};
  check(praktor_c11_native_host_init(&host, 5u, 32u, 256u) == SALTS_OK);
  check(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
  values = praktor_c11_native_host_values(&host);
  check(praktor_c11_value_store_set(values, "port", "source") == PRAKTOR_C11_VALUE_OK);
  for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    check(praktor_c11_value_store_set(values, "source", cases[i].input) == PRAKTOR_C11_VALUE_OK);
    check(praktor_c11_value_store_set(values, "delim", cases[i].delimiter) == PRAKTOR_C11_VALUE_OK);
    check(praktor_c11_value_store_set(values, "sep", cases[i].separator) == PRAKTOR_C11_VALUE_OK);
    check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
    check(praktor_c11_value_store_get(values, "source", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK);
    check(strcmp(text, cases[i].expected) == 0);
  }
  /* Empty separator used to hang Legacy. Both dispatches now reject it. */
  check(praktor_c11_value_store_set(values, "sep", "") == PRAKTOR_C11_VALUE_OK);
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  check(praktor_c11_value_store_get(values, "source", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK && strcmp(text, cases[5].expected) == 0);
  params[0].text = "missing";
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_FAILURE);
  praktor_c11_native_host_destroy(&host);
  check(praktor_c11_native_host_init(&host, 2u, 32u, 16u) == SALTS_OK);
  check(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
  values = praktor_c11_native_host_values(&host);
  request.parameter_count = 0u;
  check(praktor_c11_value_store_set(values, "parsed_output", "old") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(values, "shell_output", "a=\x01\x01\x01") == PRAKTOR_C11_VALUE_OK);
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOBUFS);
  check(praktor_c11_value_store_get(values, "parsed_output", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK && strcmp(text, "old") == 0);
  check(praktor_c11_value_store_set(values, "shell_output", "a=b") == PRAKTOR_C11_VALUE_OK);
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  check(praktor_c11_value_store_get(values, "parsed_output", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK && strcmp(text, "{\"a\":\"b\"}") == 0);
  check(praktor_c11_value_store_erase(values, "parsed_output") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(values, "full", "occupied") == PRAKTOR_C11_VALUE_OK);
  check(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOBUFS);
  check(praktor_c11_value_store_size(values) == 2u);
  praktor_c11_native_host_destroy(&host);
}

spec("C11 Native Host") {
  it("dispatches bounded typed values and fails closed") { stateless_leaves(); }
  it("owns cooperative Sleep activations per Session") { sleep_leaves(); }
  it("parses lines with Legacy newline and parameter semantics") { parse_lines(); }
  it("publishes line content and count atomically within bounds") { parse_lines_atomic_failure(); }
  it("encodes sorted key/value JSON without allocating or partial publication") { parse_key_value(); }
}
