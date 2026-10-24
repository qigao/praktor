#include "yml/yaml_document.h"
#include <errno.h>
#include <fmt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct praktor_yaml_document {
    tstr source;
    cyaml_doc_t *parsed;
};

static void clear_error(praktor_yaml_diagnostic *error) {
    if (!error) return;
    error->origin = PRAKTOR_YAML_FAILURE_NONE;
    if (error->text && error->capacity) error->text[0] = '\0';
}

static int fail(praktor_yaml_diagnostic *error, int status, const char *message) {
    if (error) {
        error->origin = status == SALTS_ENOMEM ? PRAKTOR_YAML_FAILURE_ALLOCATION : PRAKTOR_YAML_FAILURE_INPUT;
        if (error->text && error->capacity) (void)fmt(error->text, error->capacity, "{}", message);
    }
    return status;
}

static int fail_at(praktor_yaml_diagnostic *error, int status,
    const char *message, cyaml_span_t location) {
    (void)fail(error, status, message);
    if (location.start_line && error && error->text && error->capacity)
        (void)fmt(error->text, error->capacity, "{} at line {}, column {}",
            message, location.start_line, location.start_col);
    return status;
}

static int provider_fail(praktor_yaml_diagnostic *error, int status,
    const char *message, cyaml_span_t location) {
    (void)fail_at(error, status, message, location);
    if (error) error->origin = PRAKTOR_YAML_FAILURE_PROVIDER;
    return status;
}

int praktor_yaml_document_create(vstr source, praktor_yaml_document **out,
    praktor_yaml_diagnostic *error) {
    praktor_yaml_document *owner;
    cyaml_error_t diagnostic = {0};
    clear_error(error);
    if (!out || !vstr_is_valid(source) || source.len > UINT32_MAX || source.len == SIZE_MAX)
        return fail(error, SALTS_EINVAL, "Invalid YAML source or output");
    owner = (praktor_yaml_document *)malloc(sizeof(*owner));
    if (!owner) return fail(error, SALTS_ENOMEM, "Failed to allocate YAML document");
    owner->source = tstr_from_v(source);
    if (!owner->source) {
        free(owner);
        return fail(error, SALTS_ENOMEM, "Failed to retain YAML source");
    }
    owner->parsed = cyaml_parse(owner->source, tstr_len(owner->source), NULL, &diagnostic);
    if (!owner->parsed) {
        tstr_free(owner->source);
        free(owner);
        return provider_fail(error, diagnostic.code == CYAML_ERR_NOMEM ? SALTS_ENOMEM : SALTS_EINVAL,
            diagnostic.msg[0] ? diagnostic.msg : "Invalid YAML input", diagnostic.span);
    }
    *out = owner;
    return SALTS_OK;
}

void praktor_yaml_document_destroy(praktor_yaml_document *document) {
    if (!document) return;
    cyaml_free(document->parsed);
    tstr_free(document->source);
    free(document);
}

static cyaml_node_t *resolve_alias(cyaml_node_t *node) {
    unsigned hops = 0;
    while (cyaml_is_alias(node)) {
        cyaml_node_t *target = node->alias.target;
        if (!target || target == node) break;
        if (hops++ == PRAKTOR_YAML_MAX_ALIAS_HOPS) return NULL;
        node = target;
    }
    return node;
}

praktor_yaml_node praktor_yaml_root(const praktor_yaml_document *document) {
    praktor_yaml_node result = {0};
    if (document) { result.document = document->parsed; result.node = cyaml_root(document->parsed); }
    return result;
}

bool praktor_yaml_valid(praktor_yaml_node node) { return node.document && resolve_alias(node.node); }
bool praktor_yaml_is_map(praktor_yaml_node node) { return cyaml_is_map(resolve_alias(node.node)); }
bool praktor_yaml_is_seq(praktor_yaml_node node) { return cyaml_is_seq(resolve_alias(node.node)); }
bool praktor_yaml_has_val(praktor_yaml_node node) { return cyaml_is_scalar(resolve_alias(node.node)); }
bool praktor_yaml_is_string_scalar(praktor_yaml_node node) {
    cyaml_node_t *resolved = resolve_alias(node.node);
    return node.document && cyaml_is_scalar(resolved) &&
        cyaml_scalar_kind(node.document, resolved) == CYAML_KIND_STRING;
}

size_t praktor_yaml_count(praktor_yaml_node node) {
    cyaml_node_t *resolved = resolve_alias(node.node);
    return cyaml_is_map(resolved) ? cyaml_map_len(resolved) : cyaml_seq_len(resolved);
}

praktor_yaml_node praktor_yaml_at(praktor_yaml_node node, size_t index) {
    praktor_yaml_node result = {0};
    cyaml_node_t *resolved = resolve_alias(node.node);
    if (cyaml_is_map(resolved) && index < cyaml_map_len(resolved)) {
        cyaml_pair_t *pair = cyaml_map_at(resolved, (uint32_t)index);
        if (pair) { result.document = node.document; result.node = pair->val; result.key_node = pair->key; }
    } else if (cyaml_is_seq(resolved) && index < cyaml_seq_len(resolved)) {
        result.document = node.document; result.node = cyaml_seq_get(resolved, (uint32_t)index);
    }
    return result;
}

bool praktor_yaml_has(praktor_yaml_node node, const char *key) {
    return key && cyaml_has(node.document, resolve_alias(node.node), key);
}

static int scalar_text(const cyaml_doc_t *document, cyaml_node_t *node,
    tstr *out, praktor_yaml_diagnostic *error) {
    char *decoded;
    tstr result;
    node = resolve_alias(node);
    if (!out || !document || !cyaml_is_scalar(node))
        return fail(error, SALTS_EINVAL, "Expected YAML scalar");
    decoded = cyaml_scalar_str(document, node);
    if (!decoded) return provider_fail(error, SALTS_ENOMEM, "Failed to decode YAML scalar", (cyaml_span_t){0});
    result = tstr_dup(decoded);
    free(decoded);
    if (!result) return fail(error, SALTS_ENOMEM, "Failed to decode YAML scalar");
    *out = result;
    return SALTS_OK;
}

int praktor_yaml_get(praktor_yaml_node node, const char *key,
    praktor_yaml_node *out, praktor_yaml_diagnostic *error) {
    praktor_yaml_node result = {node.document, NULL, NULL};
    cyaml_node_t *mapping = resolve_alias(node.node);
    uint32_t index;
    clear_error(error);
    if (!out) return fail(error, SALTS_EINVAL, "Missing YAML node output");
    if (!key || !cyaml_is_map(mapping)) return SALTS_ENOENT;
    result.node = cyaml_get(node.document, mapping, key);
    if (!result.node) return SALTS_ENOENT;
    for (index = 0; index < cyaml_map_len(mapping); ++index) {
        cyaml_pair_t *pair = cyaml_map_at(mapping, index);
        if (pair && cyaml_is_scalar(resolve_alias(pair->key))) {
            tstr text = NULL;
            int status = scalar_text(node.document, pair->key, &text, error);
            bool matched;
            if (status != SALTS_OK) return status;
            matched = strcmp(text, key) == 0;
            tstr_free(text);
            if (matched) { result.key_node = pair->key; break; }
        }
    }
    *out = result;
    return SALTS_OK;
}

int praktor_yaml_scalar(praktor_yaml_node node, tstr *out, praktor_yaml_diagnostic *error) {
    clear_error(error);
    return scalar_text(node.document, node.node, out, error);
}

int praktor_yaml_key(praktor_yaml_node node, tstr *out, praktor_yaml_diagnostic *error) {
    clear_error(error);
    return scalar_text(node.document, node.key_node, out, error);
}

static bool find_location(cyaml_node_t *node, cyaml_span_t *out) {
    unsigned depth;
    for (depth = 0; depth < PRAKTOR_YAML_LOCATION_DEPTH; ++depth) {
        node = resolve_alias(node);
        if (!node) return false;
        if (node->span.start_line) { *out = node->span; return true; }
        if (cyaml_is_map(node) && cyaml_map_len(node)) {
            cyaml_pair_t *pair = cyaml_map_at(node, 0);
            node = pair ? pair->key : NULL;
        } else node = cyaml_seq_get(node, 0);
    }
    return false;
}

static cyaml_span_t node_location(praktor_yaml_node node) {
    cyaml_span_t result = {0};
    if (!find_location(node.key_node, &result)) (void)find_location(node.node, &result);
    return result;
}

int praktor_yaml_location(praktor_yaml_node node, tstr *out) {
    cyaml_span_t location;
    tstr result;
    if (!out) return SALTS_EINVAL;
    location = node_location(node);
    result = location.start_line ? tstr_format(" at line {}, column {}", location.start_line, location.start_col) : tstr_dup("");
    if (!result) return SALTS_ENOMEM;
    *out = result;
    return SALTS_OK;
}

int praktor_yaml_integer(praktor_yaml_node node, int *out, praktor_yaml_diagnostic *error) {
    tstr text = NULL;
    char *end;
    intmax_t parsed;
    size_t index;
    bool decimal;
    int status, saved_errno = errno, conversion_errno;
    clear_error(error);
    if (!out) return fail(error, SALTS_EINVAL, "Missing YAML integer output");
    status = scalar_text(node.document, node.node, &text, error);
    if (status != SALTS_OK) return status;
    index = text[0] == '-' ? 1u : 0u;
    decimal = index < tstr_len(text);
    for (; index < tstr_len(text); ++index)
        if (text[index] < '0' || text[index] > '9') { decimal = false; break; }
    errno = 0;
    parsed = strtoimax(text, &end, 10);
    conversion_errno = errno;
    errno = saved_errno;
    if (!decimal || end == text ||
        end != text + tstr_len(text) || conversion_errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX) {
        tstr_free(text);
        return fail_at(error, SALTS_ERANGE, "Expected YAML integer", node_location(node));
    }
    tstr_free(text);
    *out = (int)parsed;
    return SALTS_OK;
}

int praktor_yaml_bool_integer(praktor_yaml_node node, bool *out) {
    cyaml_node_t *resolved = resolve_alias(node.node);
    int64_t number;
    if (!out) return SALTS_EINVAL;
    if (!node.document || !resolved || cyaml_scalar_kind(node.document, resolved) != CYAML_KIND_INT ||
        !cyaml_as_int(node.document, resolved, &number) || (number != 0 && number != 1)) return SALTS_ENOENT;
    *out = number == 1;
    return SALTS_OK;
}
