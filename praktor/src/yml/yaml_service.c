#include "yml/yaml_service.h"
#include "yaml_diagnostic_internal.h"
#include "yml/yaml_task_fields.h"
#include "actions/variable_reference.h"
#include <core/tstr.h>
#include <string.h>
static int at_error(praktor_yaml_node n, const char *m, praktor_yaml_field_error *e) {
  return praktor_yaml_field_fail_at(n, vstr_from_cstr(m), e);
}
static int optional(praktor_yaml_node n, const char *k, praktor_yaml_node *out,
                    praktor_yaml_field_error *e) {
  return praktor_yaml_field_get(n, k, out, e);
}
static int add_value(json_value_t *o, const char *k, json_value_t *v) {
  if (!v)
    return SALTS_ENOMEM;
  if (!json_object_add_n(o, k, strlen(k), v)) {
    json_free(v);
    return SALTS_ENOMEM;
  }
  return SALTS_OK;
}
static int add_scalar(json_value_t *o, const char *k, praktor_yaml_node n, const char *message,
                      praktor_yaml_field_error *e) {
  tstr s = NULL;
  int rc = praktor_yaml_field_scalar(n, vstr_from_cstr(message), &s, e);
  if (rc == SALTS_OK)
    rc = add_value(o, k, json_create_string(s));
  tstr_free(s);
  return rc;
}
static int add_args(json_value_t *o, praktor_yaml_node n, praktor_yaml_field_error *e) {
  praktor_yaml_task_fields *owner = NULL;
  const praktor_yaml_string_list *list = NULL;
  json_value_t *args = NULL;
  int rc = praktor_yaml_task_fields_create(n, PRAKTOR_YAML_STRING_LIST, &owner, e);
  if (rc != SALTS_OK)
    return rc;
  rc = praktor_yaml_task_list(owner, &list);
  if (rc != SALTS_OK)
    goto done;
  args = json_create_array();
  if (!args) {
    rc = SALTS_ENOMEM;
    goto done;
  }
  for (size_t i = 0u; i < praktor_yaml_string_list_size(list); ++i) {
    vstr text = {0};
    rc = praktor_yaml_string_list_at(list, i, &text);
    if (rc != SALTS_OK)
      goto done;
    json_value_t *v = json_create_string_n(text.data, text.len);
    if (!v) {
      rc = SALTS_ENOMEM;
      goto done;
    }
    if (!json_array_add_checked(args, v)) {
      json_free(v);
      rc = SALTS_ENOMEM;
      goto done;
    }
  }
  rc = add_value(o, "args", args);
  args = NULL;
done:
  json_free(args);
  praktor_yaml_task_fields_destroy(owner);
  return rc;
}
static int add_capabilities(json_value_t *entry, praktor_yaml_node node,
                            praktor_yaml_field_error *error) {
  enum { MAX_CAPABILITIES = 32, MAX_CAPABILITY_BYTES = 128 };
  static const char name_chars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-";
  if (!praktor_yaml_is_seq(node) || praktor_yaml_count(node) > MAX_CAPABILITIES)
    return at_error(node, "service capabilities must be a list with at most 32 names", error);
  json_value_t *names = json_create_array();
  if (!names)
    return SALTS_ENOMEM;
  tstr name = NULL;
  int rc;
  for (size_t i = 0u; i < praktor_yaml_count(node); ++i) {
    const praktor_yaml_node item = praktor_yaml_at(node, i);
    rc = praktor_yaml_field_string(item, vstr_from_cstr("service capability must be a string"),
                                 &name, error);
    if (rc != SALTS_OK)
      goto done;
    const size_t len = tstr_len(name);
    if (!len || len > MAX_CAPABILITY_BYTES || strspn(name, name_chars) != len) {
      rc = at_error(item, "service capability must contain 1..128 ASCII letters, digits, '_', '.' or '-'", error);
      goto done;
    }
    for (size_t j = 0u; j < i; ++j) {
      if (strcmp(name, json_string(json_array_get(names, j))) == 0) {
        rc = at_error(item, "duplicate service capability", error);
        goto done;
      }
    }
    json_value_t *value = json_create_string_n(name, len);
    if (!value || !json_array_add_checked(names, value)) {
      json_free(value);
      rc = SALTS_ENOMEM;
      goto done;
    }
    tstr_free(name);
    name = NULL;
  }
  rc = add_value(entry, "capabilities", names);
  names = NULL;
done:
  tstr_free(name);
  json_free(names);
  return rc;
}
static int add_wasm_name(json_value_t *entry, praktor_yaml_node map, const char *key,
                         const char *fallback, praktor_yaml_field_error *error) {
  static const char name_chars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-";
  praktor_yaml_node field = {0};
  tstr text = NULL;
  int rc = optional(map, key, &field, error);
  if (rc != SALTS_OK) return rc;
  if (!praktor_yaml_valid(field) && fallback)
    return add_value(entry, key, json_create_string(fallback));
  rc = praktor_yaml_field_string(field, vstr_from_cstr("WASM name must be a string"), &text, error);
  if (rc == SALTS_OK) {
    size_t len = tstr_len(text);
    if (!len || len > 128u || strspn(text, name_chars) != len ||
        (!strcmp(key, "output_key") &&
          (!praktor_valid_variable_name(vstr_from_buf(text, len)) ||
           !strncmp(text, "chart.", 6u) || !strncmp(text, "event.", 6u) ||
           !strcmp(text, "shell_error"))))
      rc = at_error(field, "invalid WASM module, export or output key", error);
    else
      rc = add_value(entry, key, json_create_string_n(text, len));
  }
  tstr_free(text);
  return rc;
}
static int add_wasm_limit(json_value_t *entry, praktor_yaml_node config, const char *key,
                          int limit, praktor_yaml_field_error *error) {
  praktor_yaml_node field = {0};
  int rc = optional(config, key, &field, error);
  if (rc != SALTS_OK) return rc;
  if (praktor_yaml_valid(field) &&
      (praktor_yaml_integer(field, &limit, NULL) != SALTS_OK || limit <= 0))
    return at_error(field, "WASM fuel and timeout_ms must be positive integers", error);
  return add_value(entry, key, json_create_number(limit));
}
static int add_wasm_definition(json_value_t *entry, praktor_yaml_node provider,
                               praktor_yaml_node config, praktor_yaml_field_error *error) {
  static const vstr provider_keys[] = {{"kind", 4}, {"module", 6}};
  static const vstr config_keys[] = {{"export", 6}, {"fuel", 4}, {"timeout_ms", 10}, {"output_key", 10}};
  int rc = praktor_yaml_check_keys(provider, provider_keys, 2u, error);
  if (rc == SALTS_OK) rc = praktor_yaml_check_keys(config, config_keys, 4u, error);
  if (rc == SALTS_OK) rc = add_wasm_name(entry, provider, "module", NULL, error);
  if (rc == SALTS_OK) rc = add_wasm_name(entry, config, "export", "run", error);
  if (rc == SALTS_OK) rc = add_wasm_name(entry, config, "output_key", "wasm_output", error);
  if (rc == SALTS_OK) rc = add_wasm_limit(entry, config, "fuel", 1000000, error);
  if (rc == SALTS_OK) rc = add_wasm_limit(entry, config, "timeout_ms", 1000, error);
  return rc;
}
int praktor_yaml_service_definition_create(praktor_yaml_node item, json_value_t **out,
                                           praktor_yaml_field_error *error) {
  praktor_yaml_field_error_dispose(error);
  static const vstr service_keys[] = {{"provider", 8}, {"config", 6}, {"capabilities", 12}};
  static const vstr provider_keys[] = {{"kind", 4}, {"path", 4}};
  static const vstr config_keys[] = {{"args", 4}, {"timeout_ms", 10}, {"working_dir", 11}};
  praktor_yaml_node provider = {0}, config = {0}, value = {0}, args = {0};
  json_value_t *entry = NULL;
  tstr kind = NULL, path = NULL;
  int timeout = 300000, rc = SALTS_EINVAL;
  if (!out || !praktor_yaml_is_map(item))
    return at_error(item, "service definition must be a map", error);
  rc = praktor_yaml_check_keys(item, service_keys, sizeof service_keys / sizeof *service_keys, error);
  if (rc != SALTS_OK)
    goto done;
  rc = optional(item, "provider", &provider, error);
  if (rc != SALTS_OK)
    goto done;
  if (!praktor_yaml_is_map(provider)) {
    rc = at_error(item, "service.provider must be a map", error);
    goto done;
  }
  rc = praktor_yaml_field_get(provider, "kind", &value, error);
  if (rc != SALTS_OK)
    goto done;
  rc = praktor_yaml_field_scalar(value, vstr_from_cstr("provider.kind must be a scalar"), &kind,
                                 error);
  if (rc != SALTS_OK)
    goto done;
  const bool wasm = !strcmp(kind, "wasm");
  if (!wasm && strcmp(kind, "exe")) {
    rc = at_error(provider, "unsupported provider.kind; currently supported: exe, wasm", error);
    goto done;
  }
  if (wasm) {
    rc = optional(item, "config", &config, error);
    if (rc != SALTS_OK) goto done;
    if (!praktor_yaml_is_map(config)) {
      rc = at_error(item, "service.config must be a map", error);
      goto done;
    }
    entry = json_create_object();
    rc = entry ? add_wasm_definition(entry, provider, config, error) : SALTS_ENOMEM;
    if (rc != SALTS_OK) goto done;
    goto capabilities;
  }
  rc = praktor_yaml_check_keys(provider, provider_keys, 2u, error);
  if (rc != SALTS_OK) goto done;
  rc = praktor_yaml_field_get(provider, "path", &value, error);
  if (rc != SALTS_OK)
    goto done;
  rc = praktor_yaml_field_scalar(value, vstr_from_cstr("provider.path must be a scalar"), &path,
                                 error);
  if (rc != SALTS_OK)
    goto done;
  if (!tstr_len(path) || tstr_len(path) > 4096u) {
    rc = at_error(value, "provider.path must contain 1..4096 bytes", error);
    goto done;
  }
  rc = optional(item, "config", &config, error);
  if (rc != SALTS_OK)
    goto done;
  if (!praktor_yaml_is_map(config)) {
    rc = at_error(item, "service.config must be a map", error);
    goto done;
  }
  rc = praktor_yaml_check_keys(config, config_keys, 3u, error);
  if (rc != SALTS_OK)
    goto done;
  rc = praktor_yaml_field_get(config, "args", &args, error);
  if (rc != SALTS_OK)
    goto done;
  if (!praktor_yaml_is_seq(args) || praktor_yaml_count(args) > 256u) {
    rc = at_error(args, "service args must be a list with at most 256 entries", error);
    goto done;
  }
  value = (praktor_yaml_node){0};
  rc = optional(config, "timeout_ms", &value, error);
  if (rc != SALTS_OK)
    goto done;
  if (praktor_yaml_valid(value)) {
    rc = praktor_yaml_integer(value, &timeout, NULL);
    if (rc != SALTS_OK || timeout <= 0) {
      rc = at_error(value, "service timeout_ms must be a positive integer", error);
      goto done;
    }
  }
  entry = json_create_object();
  if (!entry) {
    rc = SALTS_ENOMEM;
    goto done;
  }
  rc = add_value(entry, "path", json_create_string(path));
  if (rc != SALTS_OK)
    goto done;
  rc = add_value(entry, "timeout_ms", json_create_number(timeout));
  if (rc != SALTS_OK)
    goto done;
  rc = add_args(entry, args, error);
  if (rc != SALTS_OK)
    goto done;
  value = (praktor_yaml_node){0};
  rc = optional(config, "working_dir", &value, error);
  if (rc != SALTS_OK)
    goto done;
  if (praktor_yaml_valid(value)) {
    rc = add_scalar(entry, "working_dir", value, "working_dir must be a scalar", error);
    if (rc != SALTS_OK)
      goto done;
  }
capabilities:
  rc = optional(item, "capabilities", &value, error);
  if (rc != SALTS_OK)
    goto done;
  if (praktor_yaml_valid(value)) {
    rc = add_capabilities(entry, value, error);
    if (rc != SALTS_OK)
      goto done;
  }
  *out = entry;
  entry = NULL;
done:
  tstr_free(kind);
  tstr_free(path);
  json_free(entry);
  if (rc == SALTS_ENOMEM && error && error->origin == PRAKTOR_YAML_FAILURE_NONE)
    error->origin = PRAKTOR_YAML_FAILURE_ALLOCATION;
  return rc;
}
