#ifndef PRAKTOR_C11_REGEX_TRE_H
#define PRAKTOR_C11_REGEX_TRE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TRE is intentionally hidden behind this API.  In particular, consumers do
 * not see regex_t and cannot accidentally bind to a platform regex ABI. */
typedef struct praktor_regex praktor_regex;

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

/* Compiles a bounded POSIX extended expression.  Back references and TRE's
 * approximate extensions are rejected so search time remains bounded by the
 * expression and input sizes.  On failure, *out_regex remains NULL. */
praktor_regex_status praktor_regex_compile(
    const char *pattern, size_t pattern_size,
    const praktor_regex_limits *limits, praktor_regex **out_regex);

size_t praktor_regex_capture_count(const praktor_regex *regex);

/* group_index zero addresses the complete match.  A valid optional group that
 * did not participate returns OK with participated=false and an empty span. */
praktor_regex_status praktor_regex_search_group(
    const praktor_regex *regex, const char *input, size_t input_size,
    size_t group_index, praktor_regex_span *out_span);

void praktor_regex_destroy(praktor_regex *regex);

#ifdef __cplusplus
}
#endif

#endif
