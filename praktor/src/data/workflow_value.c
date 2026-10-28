#include "data/workflow_value.h"
#include <fmt.h>
#include <math.h>
#include <string.h>

static int fail(char *error, size_t capacity, int status, const char *message) {
    if (error && capacity) (void)fmt(error, capacity, "{}", message);
    return status;
}

static void clear_error(char *error, size_t capacity) {
    if (error && capacity) error[0] = '\0';
}

size_t praktor_value_size(const json_value_t *value) {
    if (!value) return 0;
    switch (json_type(value)) {
    case JSON_OBJECT: return json_object_size(value);
    case JSON_ARRAY: return json_array_size(value);
    case JSON_STRING: return json_string_len(value);
    default: return 0;
    }
}

static bool integer_token(const json_value_t *value, vstr *token) {
    size_t length = 0, index;
    const char *text = json_number_text(value, &length);
    if (!text || !length) return false;
    for (index = 0; index < length; ++index)
        if (text[index] == '.' || text[index] == 'e' || text[index] == 'E') return false;
    *token = vstr_from_buf(text, length);
    return true;
}

static bool magnitude(vstr token, bool allow_negative, uint64_t positive_limit,
    uint64_t negative_limit, uint64_t *out, bool *negative) {
    size_t index = 0;
    uint64_t result = 0, limit = positive_limit;
    bool sign = token.len && token.data[0] == '-';
    if (sign) {
        if (!allow_negative) return false;
        index = 1;
        limit = negative_limit;
    }
    if (index == token.len) return false;
    for (; index < token.len; ++index) {
        unsigned digit = (unsigned char)token.data[index] - (unsigned)'0';
        if (digit > 9 || result > limit / 10 ||
            (result == limit / 10 && digit > limit % 10)) return false;
        result = result * 10 + digit;
    }
    *out = result;
    *negative = sign;
    return true;
}

bool praktor_value_is_int64(const json_value_t *value) {
    vstr token;
    uint64_t result;
    bool negative;
    return value && json_type(value) == JSON_NUMBER && integer_token(value, &token) &&
        magnitude(token, true, INT64_MAX, UINT64_C(1) << 63, &result, &negative);
}

bool praktor_value_is_uint64(const json_value_t *value) {
    vstr token;
    uint64_t result;
    bool negative;
    return value && json_type(value) == JSON_NUMBER && integer_token(value, &token) &&
        magnitude(token, false, UINT64_MAX, 0, &result, &negative);
}

static int integer_value(const json_value_t *value, unsigned bits, bool is_signed,
    uint64_t *result, bool *negative, char *error, size_t capacity) {
    vstr token;
    uint64_t positive_limit, negative_limit;
    double number, integral, upper;
    if (!value || json_type(value) != JSON_NUMBER)
        return fail(error, capacity, SALTS_EINVAL, "WorkflowValue is not a number");
    if (!bits || bits > 64)
        return fail(error, capacity, SALTS_EINVAL, "Invalid WorkflowValue integer width");
    negative_limit = is_signed ? UINT64_C(1) << (bits - 1) : 0;
    positive_limit = is_signed ? negative_limit - 1 :
        (bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1);
    if (integer_token(value, &token)) {
        if (!magnitude(token, is_signed, positive_limit, negative_limit, result, negative))
            return fail(error, capacity, SALTS_ERANGE, "WorkflowValue integer is out of range");
        return SALTS_OK;
    }
    number = json_number(value);
    upper = ldexp(1.0, (int)(is_signed ? bits - 1 : bits));
    if (!isfinite(number) || fpclassify(modf(number, &integral)) != FP_ZERO ||
        integral < (is_signed ? -upper : 0.0) || integral >= upper)
        return fail(error, capacity, SALTS_ERANGE,
            "WorkflowValue number cannot be represented as an integer");
    *negative = integral < 0.0;
    /* Magnitude is nonnegative and strictly below 2^64 (signed minimum is 2^63). */
    *result = (uint64_t)(*negative ? -integral : integral);
    return SALTS_OK;
}

int praktor_value_as_signed(const json_value_t *value, unsigned bits,
    int64_t *out, char *error, size_t capacity) {
    uint64_t result;
    bool negative;
    int status;
    clear_error(error, capacity);
    if (!out) return fail(error, capacity, SALTS_EINVAL, "Missing WorkflowValue output");
    status = integer_value(value, bits, true, &result, &negative, error, capacity);
    if (status != SALTS_OK) return status;
    *out = negative ? (result == (UINT64_C(1) << 63) ? INT64_MIN : -(int64_t)result) : (int64_t)result;
    return SALTS_OK;
}

int praktor_value_as_unsigned(const json_value_t *value, unsigned bits,
    uint64_t *out, char *error, size_t capacity) {
    uint64_t result;
    bool negative;
    int status;
    clear_error(error, capacity);
    if (!out) return fail(error, capacity, SALTS_EINVAL, "Missing WorkflowValue output");
    status = integer_value(value, bits, false, &result, &negative, error, capacity);
    if (status == SALTS_OK) *out = result;
    return status;
}

static bool valid_path(const vstr *keys, size_t count) {
    size_t index;
    if (count > PRAKTOR_VALUE_MAX_PATH || (count && !keys)) return false;
    for (index = 0; index < count; ++index)
        if (!vstr_is_valid(keys[index]) || keys[index].len == SIZE_MAX) return false;
    return true;
}

static const char *key_data(vstr key) { return key.data ? key.data : ""; }

int praktor_value_find(const json_value_t *root, const vstr *keys, size_t count,
    const json_value_t **out) {
    size_t index;
    if (!out || !valid_path(keys, count)) return SALTS_EINVAL;
    for (index = 0; index < count; ++index) {
        if (!root || json_type(root) != JSON_OBJECT) return SALTS_ENOENT;
        root = json_object_get_v(root, vstr_from_buf(key_data(keys[index]), keys[index].len));
    }
    if (!root) return SALTS_ENOENT;
    *out = root;
    return SALTS_OK;
}

static int set_value(json_value_t **root, const vstr *keys, size_t count,
    const json_value_t *source, json_value_t **take, char *error, size_t capacity) {
    json_value_t *parent, *child, *pending, *object;
    size_t index, missing = 0;
    bool replace_root, copy;
    clear_error(error, capacity);
    if (!root || !source || !valid_path(keys, count))
        return fail(error, capacity, SALTS_EINVAL, "Invalid WorkflowValue path or source");
    if (!count) {
        if (take == root) return SALTS_OK;
        pending = take ? *take : json_clone(source);
        if (!pending) return fail(error, capacity, SALTS_ENOMEM, "Failed to clone WorkflowValue");
        json_free(*root);
        *root = pending;
        if (take) *take = NULL;
        return SALTS_OK;
    }
    replace_root = !*root || json_is_null(*root);
    parent = *root;
    if (!replace_root) {
        for (index = 0; index + 1 < count; ++index) {
            if (json_type(parent) != JSON_OBJECT)
                return fail(error, capacity, SALTS_EINVAL, "WorkflowValue path traverses a non-object value");
            child = json_object_get_v(parent, vstr_from_buf(key_data(keys[index]), keys[index].len));
            if (!child) break;
            parent = child;
        }
        missing = index;
        if (json_type(parent) != JSON_OBJECT)
            return fail(error, capacity, SALTS_EINVAL, "Failed to assign WorkflowValue path");
    }
    /* Legacy self assignment snapshots after creating its parent objects.
     * Reproduce that shape on a provisional root, never on the published root.
     * Ordinary writes clone only their source and missing suffix. */
    if (source == *root && (replace_root || missing + 1 < count)) {
        json_value_t *provisional = replace_root ? json_create_object() : json_clone(*root);
        int status = SALTS_OK;
        if (!provisional)
            return fail(error, capacity, SALTS_ENOMEM, "Failed to clone WorkflowValue");
        if (count > 1) {
            json_value_t *empty = json_create_object();
            if (!empty) status = fail(error, capacity, SALTS_ENOMEM, "Failed to create nested WorkflowValue object");
            else status = set_value(&provisional, keys, count - 1, empty, &empty, error, capacity);
            json_free(empty);
        }
        if (status == SALTS_OK)
            status = set_value(&provisional, keys, count, provisional, NULL, error, capacity);
        if (status != SALTS_OK) { json_free(provisional); return status; }
        json_free(*root);
        *root = provisional;
        return SALTS_OK;
    }
    /* A detached suffix must own a clone until its last adoption succeeds:
     * the provider has no detach API for rolling back a taken source. */
    copy = !take || take == root || replace_root || missing + 1 < count;
    pending = copy ? json_clone(source) : *take;
    if (!pending) return fail(error, capacity, SALTS_ENOMEM, "Failed to clone WorkflowValue");
    for (index = count; index > missing + (replace_root ? 0u : 1u); --index) {
        object = json_create_object();
        if (!object || !json_object_add_n(object, key_data(keys[index - 1]), keys[index - 1].len, pending)) {
            json_free(object);
            json_free(pending);
            return fail(error, capacity, SALTS_ENOMEM, "Failed to create nested WorkflowValue object");
        }
        pending = object;
    }
    if (replace_root) {
        json_free(*root);
        *root = pending;
    } else if (!json_object_add_n(parent, key_data(keys[missing]), keys[missing].len, pending)) {
        if (copy) json_free(pending);
        return fail(error, capacity, SALTS_ENOMEM, "Failed to assign WorkflowValue path");
    }
    if (take && take != root) {
        if (copy) json_free(*take);
        *take = NULL;
    }
    return SALTS_OK;
}

int praktor_value_set_copy(json_value_t **root, const vstr *keys, size_t count,
    const json_value_t *source, char *error, size_t capacity) {
    return set_value(root, keys, count, source, NULL, error, capacity);
}

int praktor_value_set_take(json_value_t **root, const vstr *keys, size_t count,
    json_value_t **source, char *error, size_t capacity) {
    return set_value(root, keys, count, source ? *source : NULL, source, error, capacity);
}

static int append_value(json_value_t **root, const vstr *keys, size_t count,
    const json_value_t *source, json_value_t **take, char *error, size_t capacity) {
    const json_value_t *array = NULL;
    json_value_t *pending;
    bool copy = !take || take == root;
    clear_error(error, capacity);
    if (!root || !source || !valid_path(keys, count))
        return fail(error, capacity, SALTS_EINVAL, "Invalid WorkflowValue path or source");
    if (praktor_value_find(*root, keys, count, &array) != SALTS_OK || json_type(array) != JSON_ARRAY)
        return fail(error, capacity, SALTS_EINVAL, "WorkflowValue path is not an array");
    pending = copy ? json_clone(source) : *take;
    if (!pending) return fail(error, capacity, SALTS_ENOMEM, "Failed to clone WorkflowValue");
    if (!json_array_add_checked((json_value_t *)array, pending)) {
        if (copy) json_free(pending);
        return fail(error, capacity, SALTS_ENOMEM, "Failed to append WorkflowValue path");
    }
    if (take && take != root) *take = NULL;
    return SALTS_OK;
}

int praktor_value_append_copy(json_value_t **root, const vstr *keys, size_t count,
    const json_value_t *source, char *error, size_t capacity) {
    return append_value(root, keys, count, source, NULL, error, capacity);
}

int praktor_value_append_take(json_value_t **root, const vstr *keys, size_t count,
    json_value_t **source, char *error, size_t capacity) {
    return append_value(root, keys, count, source ? *source : NULL, source, error, capacity);
}

int praktor_value_erase(json_value_t **root, size_t index, char *error, size_t capacity) {
    json_value_t *replacement, *child;
    size_t current, count;
    clear_error(error, capacity);
    if (!root || !*root || json_type(*root) != JSON_ARRAY || index >= json_array_size(*root))
        return fail(error, capacity, SALTS_ENOENT, "WorkflowValue array index out of range");
    replacement = json_create_array();
    if (!replacement) return fail(error, capacity, SALTS_ENOMEM, "Failed to create WorkflowValue array");
    count = json_array_size(*root);
    for (current = 0; current < count; ++current) {
        if (current == index) continue;
        child = json_clone(json_array_get(*root, current));
        if (!child || !json_array_add_checked(replacement, child)) {
            json_free(child);
            json_free(replacement);
            return fail(error, capacity, SALTS_ENOMEM, "Failed to copy WorkflowValue array");
        }
    }
    json_free(*root);
    *root = replacement;
    return SALTS_OK;
}
