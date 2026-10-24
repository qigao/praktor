#ifndef PRAKTOR_WORKFLOW_VALUE_H
#define PRAKTOR_WORKFLOW_VALUE_H

#include <json_parser.h>
#include <salts/error_codes.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal migration API; the public SDK ABI is unchanged. The caller owns the
 * Salts JSON root. Views/lookup results are borrowed until the next mutation.
 * No second value representation or hidden allocator/runtime is introduced. */
#define PRAKTOR_VALUE_MAX_PATH 128u

size_t praktor_value_size(const json_value_t *value);
bool praktor_value_is_int64(const json_value_t *value);
bool praktor_value_is_uint64(const json_value_t *value);
/* bits is in [1,64]. On failure outputs are untouched; diagnostics are optional.
 * Exact integer lexemes never pass through double. Floating conversions must
 * be finite, integral, and strictly below the exact power-of-two upper bound. */
int praktor_value_as_signed(const json_value_t *value, unsigned bits,
    int64_t *out, char *error, size_t capacity);
int praktor_value_as_unsigned(const json_value_t *value, unsigned bits,
    uint64_t *out, char *error, size_t capacity);

/* Keys have explicit lengths, including embedded NULs. Empty paths select the
 * root. Only a null root is promoted; existing null/scalar children fail. */
int praktor_value_find(const json_value_t *root, const vstr *keys, size_t count,
    const json_value_t **out);
int praktor_value_set_copy(json_value_t **root, const vstr *keys, size_t count,
    const json_value_t *source, char *error, size_t capacity);
int praktor_value_set_take(json_value_t **root, const vstr *keys, size_t count,
    json_value_t **source, char *error, size_t capacity);
int praktor_value_append_copy(json_value_t **root, const vstr *keys, size_t count,
    const json_value_t *source, char *error, size_t capacity);
int praktor_value_append_take(json_value_t **root, const vstr *keys, size_t count,
    json_value_t **source, char *error, size_t capacity);
int praktor_value_erase(json_value_t **root, size_t index,
    char *error, size_t capacity);

/* All mutations commit once: on failure root and source owners are unchanged.
 * A take source must be a disjoint, uniquely owned root, or the same root slot
 * (self insertion clones). Success clears a disjoint source slot. Direct writes
 * adopt; creating a detached missing suffix clones the source before commit,
 * then releases the original owner. Pointer identity is not a take guarantee.
 * No concurrent mutation/read of the same root is allowed; independent roots
 * share no mutable state. Path work is bounded; DOM allocation follows Salts. */

#ifdef __cplusplus
}
#endif
#endif
