#ifndef PRAKTOR_OUTPUT_SCAN_H
#define PRAKTOR_OUTPUT_SCAN_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct praktor_output_span { const char *data; size_t size; } praktor_output_span;
typedef struct praktor_output_pair { praktor_output_span key, value; } praktor_output_pair;
/* Length-aware, allocation-free scanners. offset starts at zero. Return 1 for
 * an item, 0 at end, -1 for invalid arguments. Final separators add no item;
 * CR is retained by lines, pairs trim exactly space/tab/CR/LF. */
int praktor_output_next_line(praktor_output_span input, size_t *offset,
                             praktor_output_span *line);
/* Empty delimiter selects the empty key (Legacy behavior). Empty separator
 * is invalid: the old C++ implementation could loop forever on it. */
int praktor_output_next_pair(praktor_output_span input, praktor_output_span delimiter,
                             praktor_output_span separator, size_t *offset,
                             praktor_output_pair *pair);
int praktor_output_contains(praktor_output_span input, praktor_output_span search);
/* Sorted string-valued JSON object, last duplicate wins, matching std::map +
 * Salts JsonParser serialization. Caller owns all storage; no heap allocation.
 * pairs capacity >= input.size / 2 + 1 suffices (even for empty delimiter).
 * Return 0 success, -1 invalid input, -2 insufficient output/workspace.
 * Output may be partially written on failure; publish only after success.
 * Input, output and pair workspace must not overlap. Embedded NUL is rejected
 * here because the legacy JSON object's C-string keys/values truncate it. */
int praktor_output_key_value_json(praktor_output_span input,
    praktor_output_span delimiter, praktor_output_span separator,
    praktor_output_pair *pairs, size_t pair_capacity,
    char *output, size_t output_capacity);
#ifdef __cplusplus
}
#endif
#endif
