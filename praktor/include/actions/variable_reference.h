#ifndef PRAKTOR_ACTIONS_VARIABLE_REFERENCE_H
#define PRAKTOR_ACTIONS_VARIABLE_REFERENCE_H

#include <core/vstr.h>
#include <string.h>

static inline bool praktor_identifier_start(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

/* State names in Selector fields can contain hyphens. In a command, a hyphen
 * after a variable starts literal text. */
static inline size_t praktor_variable_name_size(vstr text, bool hyphen) {
    size_t end = 0u;
    if (!text.len || !praktor_identifier_start(text.data[end])) return 0u;
    while (++end < text.len) {
        const char ch = text.data[end];
        if (praktor_identifier_start(ch) || (ch >= '0' && ch <= '9') ||
            (hyphen && ch == '-')) continue;
        if (ch != '.' || end + 1u == text.len ||
            !praktor_identifier_start(text.data[end + 1u])) break;
    }
    return end;
}

static inline bool praktor_retired_variable_namespace(vstr name) {
    return name.len > 5u && !memcmp(name.data, "vars.", 5u);
}

static inline vstr praktor_variable_reference(vstr text) {
    if (text.len >= 2u && text.data[0] == '$') {
        const vstr name = {text.data + 1u, text.len - 1u};
        if (praktor_variable_name_size(name, true) == name.len &&
            !praktor_retired_variable_namespace(name)) return name;
    }
    return (vstr){0};
}

static inline bool praktor_retired_variable_reference(vstr text) {
    if (text.len >= 3u && text.data[0] == '{' && text.data[text.len - 1u] == '}') {
        const vstr name = {text.data + 1u, text.len - 2u};
        return praktor_variable_name_size(name, true) == name.len;
    }
    if (text.len >= 2u && text.data[0] == '$') {
        const vstr name = {text.data + 1u, text.len - 1u};
        return praktor_retired_variable_namespace(name) ||
               (name.data[0] == '{' && name.data[name.len - 1u] == '}');
    }
    return false;
}

#endif
