#include "yml/yaml_value.h"
#include "yaml_diagnostic_internal.h"
#include <ctype.h>
#include <errno.h>
#include <fmt.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static int json_failure(praktor_yaml_field_error *error, const char *message) {
    if (!error) return SALTS_ENOMEM;
    return praktor_yaml_field_fail(error, SALTS_ENOMEM, PRAKTOR_YAML_FAILURE_PROVIDER, tstr_dup(message));
}

static bool lower_is(const char *text, const char *word) {
    while (*text && *word) {
        if (tolower((unsigned char)*text++) != (unsigned char)*word++) return false;
    }
    return !*text && !*word;
}

static bool signed_decimal(tstr text, int64_t *out) {
    size_t index = text[0] == '-' ? 1u : 0u;
    intmax_t value;
    char *end;
    int saved_errno, conversion_errno;
    if (index == tstr_len(text)) return false;
    for (; index < tstr_len(text); ++index)
        if (text[index] < '0' || text[index] > '9') return false;
    saved_errno = errno;
    errno = 0;
    value = strtoimax(text, &end, 10);
    conversion_errno = errno;
    errno = saved_errno;
    if (conversion_errno == ERANGE || end != text + tstr_len(text) || value < INT64_MIN || value > INT64_MAX) return false;
    *out = (int64_t)value;
    return true;
}

static int scalar_value(praktor_yaml_node node, json_value_t **out, praktor_yaml_field_error *error) {
    tstr text = NULL;
    json_value_t *result;
    int64_t integer;
    const char *failure;
    int status = praktor_yaml_field_decode(node, false, &text, error);
    if (status != SALTS_OK) return status;
    if (praktor_yaml_is_string_scalar(node)) {
        result = json_create_string_n(text, tstr_len(text));
        failure = "Turbo Parser failed to create string";
    } else if (lower_is(text, "null") || lower_is(text, "~")) {
        result = json_create_null();
        failure = "Turbo Parser failed to create null";
    } else if (lower_is(text, "true") || lower_is(text, "yes") || lower_is(text, "on") ||
        lower_is(text, "false") || lower_is(text, "no") || lower_is(text, "off")) {
        result = json_create_bool(lower_is(text, "true") || lower_is(text, "yes") || lower_is(text, "on"));
        failure = "Turbo Parser failed to create boolean";
    } else if (signed_decimal(text, &integer)) {
        result = json_create_int64(integer);
        failure = "Turbo Parser failed to create integer";
    } else {
        char *end = NULL;
        double number = strtod(text, &end);
        /* Retain the original full-consumption check, including empty text. */
        if (end == text + tstr_len(text)) {
            result = json_create_number(number);
            failure = "Turbo Parser failed to create number";
        } else {
            result = json_create_string_n(text, tstr_len(text));
            failure = "Turbo Parser failed to create string";
        }
    }
    tstr_free(text);
    if (!result) return json_failure(error, failure);
    *out = result;
    return SALTS_OK;
}

static int limit_error(praktor_yaml_node node, const char *prefix, size_t maximum,
    praktor_yaml_field_error *error) {
    tstr message;
    int status;
    if (!error) return SALTS_EINVAL;
    message = tstr_format("{}{}", prefix, maximum);
    if (!message) return praktor_yaml_field_fail(error, SALTS_ENOMEM, PRAKTOR_YAML_FAILURE_ALLOCATION, NULL);
    status = praktor_yaml_field_fail_at(node, tstr_to_v(message), error);
    tstr_free(message);
    return status;
}

static int convert_value(praktor_yaml_node node, unsigned depth, size_t *remaining,
    json_value_t **out, praktor_yaml_field_error *error) {
    bool mapping;
    json_value_t *result;
    size_t index;
    if (depth > PRAKTOR_YAML_VALUE_MAX_DEPTH)
        return limit_error(node, "YAML value nesting exceeds maximum depth of ", PRAKTOR_YAML_VALUE_MAX_DEPTH, error);
    if (!*remaining)
        return limit_error(node, "YAML value expansion exceeds maximum count of ", PRAKTOR_YAML_VALUE_MAX_NODES, error);
    --*remaining;
    mapping = praktor_yaml_is_map(node);
    if (!mapping && !praktor_yaml_is_seq(node)) {
        if (!praktor_yaml_has_val(node))
            return praktor_yaml_field_fail_at(node, vstr_from_cstr("contract value must be a scalar, sequence, or map"), error);
        return scalar_value(node, out, error);
    }
    result = mapping ? json_create_object() : json_create_array();
    if (!result) return json_failure(error, "Turbo Parser failed to adopt JSON value");
    for (index = 0; index < praktor_yaml_count(node); ++index) {
        praktor_yaml_node child = praktor_yaml_at(node, index);
        json_value_t *value = NULL;
        tstr key = NULL;
        bool committed;
        int status = convert_value(child, depth + 1, remaining, &value, error);
        if (status == SALTS_OK && mapping) status = praktor_yaml_field_decode(child, true, &key, error);
        if (status != SALTS_OK) { json_free(value); json_free(result); return status; }
        committed = mapping ? json_object_add_n(result, key, tstr_len(key), value) : json_array_add_checked(result, value);
        tstr_free(key);
        if (!committed) {
            json_free(value);
            json_free(result);
            return json_failure(error, mapping ? "Failed to assign WorkflowValue path" : "Failed to append WorkflowValue array member");
        }
    }
    *out = result;
    return SALTS_OK;
}

int praktor_yaml_value(praktor_yaml_node node, json_value_t **out, praktor_yaml_field_error *error) {
    size_t remaining = PRAKTOR_YAML_VALUE_MAX_NODES;
    praktor_yaml_field_error_dispose(error);
    if (!out) {
        if (!error) return SALTS_EINVAL;
        return praktor_yaml_field_fail(error, SALTS_EINVAL, PRAKTOR_YAML_FAILURE_INPUT,
            tstr_dup("Missing YAML value output"));
    }
    return convert_value(node, 1, &remaining, out, error);
}
