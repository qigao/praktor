#include "data/workflow_contract_value.h"

#include <string.h>

bool praktor_contract_value_matches_type(const json_value_t *value,
                                         const char *type) {
    if (value == NULL || type == NULL) return false;
    if (strcmp(type, "string") == 0) return json_type(value) == JSON_STRING;
    if (strcmp(type, "boolean") == 0) return json_type(value) == JSON_BOOL;
    if (strcmp(type, "integer") == 0)
        return praktor_value_is_int64(value) || praktor_value_is_uint64(value);
    if (strcmp(type, "number") == 0) return json_type(value) == JSON_NUMBER;
    if (strcmp(type, "array") == 0) return json_type(value) == JSON_ARRAY;
    if (strcmp(type, "object") == 0) return json_type(value) == JSON_OBJECT;
    return false;
}

int praktor_contract_value_in_enum(const json_value_t *value,
                                   const json_value_t *enum_values,
                                   bool *out) {
    char *encoded_value;
    size_t encoded_value_size = 0, index, count;
    bool found = false;
    if (value == NULL || out == NULL) return SALTS_EINVAL;
    if (enum_values == NULL) { *out = true; return SALTS_OK; }
    if (json_type(enum_values) != JSON_ARRAY) return SALTS_EINVAL;
    count = json_array_size(enum_values);
    if (count == 0u) { *out = true; return SALTS_OK; }
    encoded_value = json_serialize(value, &encoded_value_size);
    if (encoded_value == NULL) return SALTS_ENOMEM;
    for (index = 0; index < count; ++index) {
        const json_value_t *candidate = json_array_get(enum_values, index);
        char *encoded_candidate;
        size_t encoded_candidate_size = 0;
        if (candidate == NULL) {
            json_serialize_free(encoded_value);
            return SALTS_EINVAL;
        }
        encoded_candidate = json_serialize(candidate, &encoded_candidate_size);
        if (encoded_candidate == NULL) {
            json_serialize_free(encoded_value);
            return SALTS_ENOMEM;
        }
        if (encoded_candidate_size == encoded_value_size &&
            memcmp(encoded_candidate, encoded_value, encoded_value_size) == 0)
            found = true;
        json_serialize_free(encoded_candidate);
        if (found) break;
    }
    json_serialize_free(encoded_value);
    *out = found;
    return SALTS_OK;
}

static int schema_add(json_value_t *object, const char *key,
                      json_value_t *value) {
    if (value == NULL) return SALTS_ENOMEM;
    if (!json_object_add_n(object, key, strlen(key), value)) {
        json_free(value);
        return SALTS_ENOMEM;
    }
    return SALTS_OK;
}

int praktor_contract_schema_field(const char *type, const char *description,
                                  bool has_description,
                                  const json_value_t *default_value,
                                  const json_value_t *enum_values,
                                  json_value_t **out) {
    json_value_t *schema = NULL, *value = NULL;
    int status;
    if (out == NULL) return SALTS_EINVAL;
    *out = NULL;
    if (type == NULL || (has_description && description == NULL) ||
        (enum_values != NULL && json_type(enum_values) != JSON_ARRAY))
        return SALTS_EINVAL;
    schema = json_create_object();
    if (schema == NULL) return SALTS_ENOMEM;
    value = json_create_string(type);
    status = schema_add(schema, "type", value);
    if (status != SALTS_OK) goto fail;
    if (has_description) {
        value = json_create_string(description);
        status = schema_add(schema, "description", value);
        if (status != SALTS_OK) goto fail;
    }
    if (default_value != NULL) {
        value = json_clone(default_value);
        status = schema_add(schema, "default", value);
        if (status != SALTS_OK) goto fail;
    }
    if (enum_values != NULL && json_array_size(enum_values) != 0u) {
        value = json_clone(enum_values);
        status = schema_add(schema, "enum", value);
        if (status != SALTS_OK) goto fail;
    }
    *out = schema;
    return SALTS_OK;
fail:
    json_free(schema);
    return status;
}
