#include "yaml_diagnostic_internal.h"
#include <ctype.h>
#include <fmt.h>
#include <stdint.h>
#include <string.h>

void praktor_yaml_field_error_dispose(praktor_yaml_field_error *error) {
    if (!error) return;
    tstr_free(error->text);
    error->text = NULL;
    error->origin = PRAKTOR_YAML_FAILURE_NONE;
}

int praktor_yaml_field_fail(praktor_yaml_field_error *error, int status,
    praktor_yaml_failure origin, tstr message) {
    bool allocated = message != NULL;
    if (error) {
        error->text = message;
        error->origin = message ? origin : PRAKTOR_YAML_FAILURE_ALLOCATION;
    } else tstr_free(message);
    return allocated ? status : SALTS_ENOMEM;
}

static int invalid_argument(praktor_yaml_field_error *error) {
    if (!error) return SALTS_EINVAL;
    return praktor_yaml_field_fail(error, SALTS_EINVAL, PRAKTOR_YAML_FAILURE_INPUT,
        tstr_dup("Invalid YAML field argument"));
}

int praktor_yaml_field_fail_at(praktor_yaml_node node, vstr message,
    praktor_yaml_field_error *error) {
    tstr location = NULL, text;
    int status;
    if (!error) return SALTS_EINVAL;
    status = praktor_yaml_location(node, &location);
    if (status != SALTS_OK)
        return praktor_yaml_field_fail(error, status, PRAKTOR_YAML_FAILURE_ALLOCATION, NULL);
    text = tstr_format("Parse error{}: {}", location, message);
    tstr_free(location);
    return praktor_yaml_field_fail(error, SALTS_EINVAL, PRAKTOR_YAML_FAILURE_INPUT, text);
}

int praktor_yaml_field_decode(praktor_yaml_node node, bool key, tstr *out,
    praktor_yaml_field_error *error) {
    char buffer[256];
    praktor_yaml_diagnostic diagnostic = {buffer, sizeof(buffer), PRAKTOR_YAML_FAILURE_NONE};
    int status = key ? praktor_yaml_key(node, out, &diagnostic) : praktor_yaml_scalar(node, out, &diagnostic);
    if (status == SALTS_OK || !error) return status;
    return praktor_yaml_field_fail(error, status, diagnostic.origin, tstr_dup(buffer));
}

static int read_scalar(praktor_yaml_node node, vstr message, bool string_only,
    tstr *out, praktor_yaml_field_error *error) {
    tstr text = NULL;
    int status;
    if (!out || !vstr_is_valid(message)) return invalid_argument(error);
    if (!praktor_yaml_has_val(node) || (string_only && !praktor_yaml_is_string_scalar(node)))
        return praktor_yaml_field_fail_at(node, message, error);
    status = praktor_yaml_field_decode(node, false, &text, error);
    if (status != SALTS_OK) return status;
    if (tstr_len(text) > PRAKTOR_YAML_MAX_PARAMETER_LENGTH) {
        tstr_free(text);
        return praktor_yaml_field_fail_at(node,
            vstr_from_cstr("Parameter value exceeds maximum length of 10240 bytes"), error);
    }
    *out = text;
    return SALTS_OK;
}

int praktor_yaml_field_scalar(praktor_yaml_node node, vstr message, tstr *out,
    praktor_yaml_field_error *error) {
    praktor_yaml_field_error_dispose(error);
    return read_scalar(node, message, false, out, error);
}

int praktor_yaml_field_string(praktor_yaml_node node, vstr message, tstr *out,
    praktor_yaml_field_error *error) {
    praktor_yaml_field_error_dispose(error);
    return read_scalar(node, message, true, out, error);
}

int praktor_yaml_force_terminate_integer(vstr value, bool *out) {
    size_t position = 0;
    unsigned base = 10;
    uint64_t number = 0;
    bool negative = false, digits = false;
    if (!out || !vstr_is_valid(value)) return SALTS_EINVAL;
    if (!value.len) return SALTS_ENOENT;
    if (value.data[0] == '+' || value.data[0] == '-') {
        negative = value.data[0] == '-';
        if (++position == value.len) return SALTS_ENOENT;
    }
    if (value.len - position > 1 && value.data[position] == '0') {
        switch (value.data[position + 1]) {
        case 'b': base = 2; break;
        case 'o': base = 8; break;
        case 'x': base = 16; break;
        default: break;
        }
        if (base != 10) position += 2;
    }
    if (position == value.len ||
        (base == 10 && !isdigit((unsigned char)value.data[position]))) return SALTS_ENOENT;
    for (; position < value.len; ++position) {
        unsigned digit;
        unsigned char ch = (unsigned char)value.data[position];
        if (ch == '_') continue;
        if (ch >= '0' && ch <= '9') digit = (unsigned)(ch - '0');
        else if (ch >= 'a' && ch <= 'f') digit = (unsigned)(ch - 'a') + 10u;
        else if (ch >= 'A' && ch <= 'F') digit = (unsigned)(ch - 'A') + 10u;
        else return SALTS_ENOENT;
        if (digit >= base || number > (UINT64_MAX - digit) / base) return SALTS_ENOENT;
        number = number * base + digit;
        digits = true;
    }
    if (!digits || number > 1 || (negative && number != 0)) return SALTS_ENOENT;
    *out = number == 1;
    return SALTS_OK;
}

int praktor_yaml_field_boolean(praktor_yaml_node node, vstr field_name,
    praktor_yaml_boolean_policy policy, bool *out, praktor_yaml_field_error *error) {
    tstr message, text = NULL;
    bool value;
    int status;
    size_t index;
    praktor_yaml_field_error_dispose(error);
    if (!out || !vstr_is_valid(field_name) ||
        (policy != PRAKTOR_YAML_BOOLEAN_STRICT && policy != PRAKTOR_YAML_BOOLEAN_FORCE_TERMINATE))
        return invalid_argument(error);
    message = tstr_format("'{}' must be a boolean", field_name);
    if (!message) return praktor_yaml_field_fail(error, SALTS_ENOMEM, PRAKTOR_YAML_FAILURE_ALLOCATION, NULL);
    status = read_scalar(node, tstr_to_v(message), false, &text, error);
    if (status != SALTS_OK) { tstr_free(message); return status; }
    for (index = 0; index < tstr_len(text); ++index) text[index] = (char)tolower((unsigned char)text[index]);
    if (!strcmp(text, "true") || !strcmp(text, "yes") || !strcmp(text, "1")) value = true;
    else if (!strcmp(text, "false") || !strcmp(text, "no") || !strcmp(text, "0")) value = false;
    else if (policy == PRAKTOR_YAML_BOOLEAN_FORCE_TERMINATE && !strcmp(text, "on")) value = true;
    else if (policy == PRAKTOR_YAML_BOOLEAN_FORCE_TERMINATE && !strcmp(text, "off")) value = false;
    else if (policy != PRAKTOR_YAML_BOOLEAN_FORCE_TERMINATE ||
        (praktor_yaml_bool_integer(node, &value) != SALTS_OK &&
         praktor_yaml_force_terminate_integer(tstr_to_v(text), &value) != SALTS_OK)) {
        tstr_free(text);
        status = praktor_yaml_field_fail_at(node, tstr_to_v(message), error);
        tstr_free(message);
        return status;
    }
    tstr_free(text);
    tstr_free(message);
    *out = value;
    return SALTS_OK;
}

int praktor_yaml_check_keys(praktor_yaml_node node, const vstr *allowed,
    size_t count, praktor_yaml_field_error *error) {
    size_t index, candidate;
    praktor_yaml_field_error_dispose(error);
    if (count && !allowed) return invalid_argument(error);
    for (candidate = 0; candidate < count; ++candidate)
        if (!vstr_is_valid(allowed[candidate])) return invalid_argument(error);
    if (!praktor_yaml_is_map(node)) return SALTS_OK;
    for (index = 0; index < praktor_yaml_count(node); ++index) {
        praktor_yaml_node child = praktor_yaml_at(node, index);
        tstr key = NULL, message;
        int status = praktor_yaml_field_decode(child, true, &key, error);
        if (status != SALTS_OK) return status;
        for (candidate = 0; candidate < count; ++candidate)
            if (vstr_eq(tstr_to_v(key), allowed[candidate])) break;
        if (candidate == count) {
            if (!error) { tstr_free(key); return SALTS_EINVAL; }
            message = tstr_format("Unknown key: '{}'", key);
            tstr_free(key);
            if (!message) return praktor_yaml_field_fail(error, SALTS_ENOMEM, PRAKTOR_YAML_FAILURE_ALLOCATION, NULL);
            status = praktor_yaml_field_fail_at(child, tstr_to_v(message), error);
            tstr_free(message);
            return status;
        }
        tstr_free(key);
    }
    return SALTS_OK;
}
