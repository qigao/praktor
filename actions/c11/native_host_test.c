#include "native_host.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); return 1; } } while (0)
int main(void) {
  praktor_c11_native_host host = {0};
  praktor_c11_bt_host hooks = {0};
  praktor_c11_value_store *values;
  praktor_c11_leaf_parameter params[2] = {
      {"key", PRAKTOR_C11_LEAF_STRING, "answer", 0.0, false},
      {"value", PRAKTOR_C11_LEAF_STRING, "{source}", 0.0, false}};
  praktor_c11_leaf_request request = {0u, "SetVariable", params, 2u};
  salts_bt_node_status status = SALTS_BT_FAILURE;
  char text[65]; size_t size = 0u;
  CHECK(praktor_c11_native_host_init(&host, 2u, 32u, 64u) == SALTS_OK);
  CHECK(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
  values = praktor_c11_native_host_values(&host);
  CHECK(praktor_c11_value_store_set(values, "source", "value") == PRAKTOR_C11_VALUE_OK);
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  CHECK(praktor_c11_value_store_get(values, "answer", text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK);
  CHECK(strcmp(text, "value") == 0);
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
  request.parameter_count = 0u; request.action = "Sleep";
  CHECK(hooks.tick_typed_leaf(hooks.user, &request, &status) == SALTS_ENOTSUP);
  praktor_c11_native_host_destroy(&host);
  CHECK(host.impl == NULL);
  return 0;
}
