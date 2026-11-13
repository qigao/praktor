#ifndef PRAKTOR_REGEX_PCRE2_H
#define PRAKTOR_REGEX_PCRE2_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The 8-bit PCRE2 implementation stays private. A handle owns reusable match
 * data: serialize searches/destruction on the same handle. Independent handles
 * can run concurrently. Offsets and explicit lengths are always byte counts. */
typedef struct praktor_regex praktor_regex;

#define PRAKTOR_REGEX_HARD_CAPTURE_LIMIT 64u
#define PRAKTOR_REGEX_MAX_PATTERN_BYTES (64u * 1024u)
#define PRAKTOR_REGEX_MAX_COMPILED_BYTES (1024u * 1024u)
#define PRAKTOR_REGEX_MATCH_LIMIT 100000u
#define PRAKTOR_REGEX_DEPTH_LIMIT 256u
#define PRAKTOR_REGEX_HEAP_LIMIT_KIB 1024u

typedef enum praktor_regex_status {
  PRAKTOR_REGEX_OK = 0,
  PRAKTOR_REGEX_NO_MATCH,
  PRAKTOR_REGEX_INVALID_PATTERN,
  PRAKTOR_REGEX_UNSUPPORTED,
  PRAKTOR_REGEX_LIMIT,
  PRAKTOR_REGEX_NOMEM,
  PRAKTOR_REGEX_ENGINE
} praktor_regex_status;

typedef struct praktor_regex_limits {
  size_t max_capture_count;
} praktor_regex_limits;

typedef struct praktor_regex_span {
  size_t offset;
  size_t size;
  bool participated;
} praktor_regex_span;

/* Compiles native PCRE2 syntax with leftmost-first matching, backreferences and
 * lookarounds. Byte mode is the default; patterns may explicitly enable UTF.
 * Compilation, captures and interpretive matching have the above hard budgets;
 * they are work/memory limits, not wall-clock deadlines. Search may allocate
 * bounded cached heap frames. On failure, *out_regex remains NULL. */
praktor_regex_status praktor_regex_compile(const char *pattern, size_t pattern_size,
                                           const praktor_regex_limits *limits,
                                           praktor_regex **out_regex);

size_t praktor_regex_capture_count(const praktor_regex *regex);

/* group_index zero addresses the complete match.  A valid optional group that
 * did not participate returns OK with participated=false and an empty span. */
praktor_regex_status praktor_regex_search_group(const praktor_regex *regex, const char *input,
                                                size_t input_size, size_t group_index,
                                                praktor_regex_span *out_span);

void praktor_regex_destroy(praktor_regex *regex);

#ifdef __cplusplus
}
#endif

#endif
