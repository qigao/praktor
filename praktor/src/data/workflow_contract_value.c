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
