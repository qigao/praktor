#include "native_host.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); return 1; } } while (0)
static int stateless_leaves(void) {
  praktor_c11_native_host host = {0};
  praktor_c11_bt_host hooks = {0};
  praktor_c11_value_store *values;
  praktor_c11_leaf_parameter params[2] = {
      {"key", PRAKTOR_C11_LEAF_STRING, "answer", 0.0, false},
      {"value", PRAKTOR_C11_LEAF_STRING, "{source}", 0.0, false}};
  praktor_c11_leaf_request request = {0u, "SetVariable", params, 2u, false, NULL};
  salts_bt_node_status status = SALTS_BT_FAILURE;
  char text[65]; size_t size = 0u;
  CHECK(praktor_c11_native_host_init(&host, 2u, 32u, 64u) == SALTS_OK);
  CHECK(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
  values = praktor_c11_native_host_values(&host);
  CHECK(praktor_c11_value_store_set(values, "source", "value") == PRAKTOR_C11_VALUE_OK);
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  CHECK(praktor_c11_value_store_get(values, "answer", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK);
  CHECK(strcmp(text, "value") == 0);
  request.in_parallel_branch = true;
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOTSUP);
  request.in_parallel_branch = false;
  params[1].kind = PRAKTOR_C11_LEAF_NUMBER; params[1].number = NAN;
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  CHECK(praktor_c11_value_store_get(values, "answer", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK && strcmp(text, "value") == 0);
  params[1].number = 7.0;
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK);
  CHECK(praktor_c11_value_store_get(values, "answer", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK && strcmp(text, "7.000000") == 0);
  params[0].text = "overflow";
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOBUFS);
  CHECK(praktor_c11_value_store_size(values) == 2u);
  request.action = "CheckExitCode";
  params[0].name = "input_key"; params[0].text = "answer";
  params[1].name = "expected"; params[1].number = 7.0;
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  params[1].number = 7.25;
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  params[1].kind = PRAKTOR_C11_LEAF_BOOLEAN;
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  params[1].name = NULL;
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  request.parameter_count = 0u; request.action = "Unregistered";
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOTSUP);
  praktor_c11_native_host_destroy(&host);
  CHECK(host.impl == NULL);
  return 0;
}

static uint64_t fake_now(void *user) { return *(uint64_t *)user; }

static int sleep_leaves(void) {
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
  CHECK(praktor_c11_native_host_init_with_config(&host, &config) == SALTS_OK);
  CHECK(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
  CHECK(hooks.halt_leaf == NULL && hooks.halt_typed_leaf != NULL);
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_RUNNING);
  now = UINT64_C(1999000000);
  duration.text = "0ms";
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_RUNNING);
  now = UINT64_C(2000000000);
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  for (i = 0u; i < sizeof(supported) / sizeof(supported[0]); ++i) {
    now = 0u; duration.text = supported[i].text;
    CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_RUNNING);
    now = supported[i].ns - 1u;
    CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_RUNNING);
    ++now;
    CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  }
  for (i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    duration.text = invalid[i];
    CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  }
  duration.kind = PRAKTOR_C11_LEAF_NUMBER; duration.number = NAN;
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  duration.number = -1.0;
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  duration.number = 0.9;
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  duration.kind = PRAKTOR_C11_LEAF_BOOLEAN;
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  duration.kind = PRAKTOR_C11_LEAF_STRING; duration.text = "1s";
  now = 100u;
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_RUNNING);
  other.session_owner = &owner_b;
  CHECK(hooks.tick_typed_leaf(hooks.user, &other, &status) == SALTS_ENOBUFS);
  hooks.halt_typed_leaf(hooks.user, &other);
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_RUNNING);
  now = 99u;
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  hooks.halt_typed_leaf(hooks.user, &request);
  CHECK(hooks.tick_typed_leaf(hooks.user, &other, &status) == SALTS_OK && status == SALTS_BT_RUNNING);
  hooks.halt_typed_leaf(hooks.user, &other);
  request.session_owner = NULL;
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_EINVAL);
  request.session_owner = &owner_a; request.in_parallel_branch = true;
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOTSUP);
  praktor_c11_native_host_destroy(&host);
  config.max_running_leaves = 0u;
  CHECK(praktor_c11_native_host_init_with_config(&host, &config) == SALTS_OK);
  CHECK(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
  request.in_parallel_branch = false;
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOBUFS);
  duration.text = "0ms";
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  praktor_c11_native_host_destroy(&host);
  return 0;
}

int main(void) {
  if (stateless_leaves() != 0) return 1;
  return sleep_leaves();
}
