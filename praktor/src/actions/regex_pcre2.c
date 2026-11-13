#include "actions/regex_pcre2.h"
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <stdint.h>
#include <stdlib.h>

struct praktor_regex {
  pcre2_code *expression;
  pcre2_match_data *matches;
  pcre2_match_context *context;
  size_t capture_count;
};
static praktor_regex_status compile_error(int error) {
  switch (error) {
  case PCRE2_ERROR_HEAP_FAILED:
    return PRAKTOR_REGEX_NOMEM;
  case PCRE2_ERROR_PARENTHESES_NEST_TOO_DEEP:
  case PCRE2_ERROR_PARENTHESES_STACK_CHECK:
  case PCRE2_ERROR_PATTERN_TOO_LARGE:
  case PCRE2_ERROR_INTERNAL_CODE_OVERFLOW:
  case PCRE2_ERROR_PATTERN_TOO_COMPLICATED:
  case PCRE2_ERROR_PATTERN_STRING_TOO_LONG:
  case PCRE2_ERROR_TOO_MANY_CAPTURES:
    return PRAKTOR_REGEX_LIMIT;
  default:
    return PRAKTOR_REGEX_INVALID_PATTERN;
  }
}
praktor_regex_status praktor_regex_compile(const char *pattern, size_t pattern_size,
                                           const praktor_regex_limits *limits,
                                           praktor_regex **out_regex) {
  if (!out_regex)
    return PRAKTOR_REGEX_ENGINE;
  *out_regex = NULL;
  if (!pattern || !limits)
    return PRAKTOR_REGEX_ENGINE;
  if (limits->max_capture_count > PRAKTOR_REGEX_HARD_CAPTURE_LIMIT ||
      pattern_size > PRAKTOR_REGEX_MAX_PATTERN_BYTES)
    return PRAKTOR_REGEX_LIMIT;
  pcre2_compile_context *compile = pcre2_compile_context_create(NULL);
  if (!compile)
    return PRAKTOR_REGEX_NOMEM;
  /* Use the interpreter with explicit budgets; no JIT with a different stack
   * policy, alternate matcher, or fallback semantics is installed. */
  if (pcre2_set_max_pattern_length(compile, PRAKTOR_REGEX_MAX_PATTERN_BYTES) != 0 ||
      pcre2_set_max_pattern_compiled_length(compile, PRAKTOR_REGEX_MAX_COMPILED_BYTES) != 0 ||
      pcre2_set_parens_nest_limit(compile, PRAKTOR_REGEX_DEPTH_LIMIT) != 0) {
    pcre2_compile_context_free(compile);
    return PRAKTOR_REGEX_ENGINE;
  }
  praktor_regex *regex = calloc(1u, sizeof(*regex));
  if (!regex) {
    pcre2_compile_context_free(compile);
    return PRAKTOR_REGEX_NOMEM;
  }
  int error = 0;
  PCRE2_SIZE offset = 0u;
  regex->expression =
      pcre2_compile((PCRE2_SPTR)pattern, (PCRE2_SIZE)pattern_size, 0u, &error, &offset, compile);
  pcre2_compile_context_free(compile);
  if (!regex->expression) {
    free(regex);
    return compile_error(error);
  }
  uint32_t captures = 0u;
  if (pcre2_pattern_info(regex->expression, PCRE2_INFO_CAPTURECOUNT, &captures) != 0) {
    praktor_regex_destroy(regex);
    return PRAKTOR_REGEX_ENGINE;
  }
  if (captures > limits->max_capture_count) {
    praktor_regex_destroy(regex);
    return PRAKTOR_REGEX_LIMIT;
  }
  regex->capture_count = captures;
  regex->matches = pcre2_match_data_create_from_pattern(regex->expression, NULL);
  regex->context = pcre2_match_context_create(NULL);
  if (!regex->matches || !regex->context) {
    praktor_regex_destroy(regex);
    return PRAKTOR_REGEX_NOMEM;
  }
  if (pcre2_set_match_limit(regex->context, PRAKTOR_REGEX_MATCH_LIMIT) != 0 ||
      pcre2_set_depth_limit(regex->context, PRAKTOR_REGEX_DEPTH_LIMIT) != 0 ||
      pcre2_set_heap_limit(regex->context, PRAKTOR_REGEX_HEAP_LIMIT_KIB) != 0) {
    praktor_regex_destroy(regex);
    return PRAKTOR_REGEX_ENGINE;
  }
  *out_regex = regex;
  return PRAKTOR_REGEX_OK;
}
size_t praktor_regex_capture_count(const praktor_regex *regex) {
  return regex ? regex->capture_count : 0u;
}
praktor_regex_status praktor_regex_search_group(const praktor_regex *regex, const char *input,
                                                size_t input_size, size_t group_index,
                                                praktor_regex_span *out_span) {
  if (!out_span)
    return PRAKTOR_REGEX_ENGINE;
  *out_span = (praktor_regex_span){0u, 0u, false};
  if (!regex || !input || input_size == PCRE2_ZERO_TERMINATED || group_index > regex->capture_count)
    return PRAKTOR_REGEX_ENGINE;
  int result = pcre2_match(regex->expression, (PCRE2_SPTR)input, (PCRE2_SIZE)input_size, 0u, 0u,
                           regex->matches, regex->context);
  switch (result) {
  case PCRE2_ERROR_NOMATCH:
    return PRAKTOR_REGEX_NO_MATCH;
  case PCRE2_ERROR_MATCHLIMIT:
  case PCRE2_ERROR_DEPTHLIMIT:
  case PCRE2_ERROR_HEAPLIMIT:
    return PRAKTOR_REGEX_LIMIT;
  case PCRE2_ERROR_NOMEMORY:
    return PRAKTOR_REGEX_NOMEM;
  default:
    if (result <= 0)
      return PRAKTOR_REGEX_ENGINE;
  }
  /* Groups above the highest participating capture must not expose offsets
   * retained in this reusable match-data block by an earlier invocation. */
  if (group_index >= (size_t)result)
    return PRAKTOR_REGEX_OK;
  PCRE2_SIZE *matches = pcre2_get_ovector_pointer(regex->matches);
  PCRE2_SIZE begin = matches[2u * group_index], end = matches[2u * group_index + 1u];
  if (begin == PCRE2_UNSET || end == PCRE2_UNSET)
    return PRAKTOR_REGEX_OK;
  if (begin > input_size || end > input_size || end < begin)
    return PRAKTOR_REGEX_ENGINE;
  *out_span = (praktor_regex_span){(size_t)begin, (size_t)(end - begin), true};
  return PRAKTOR_REGEX_OK;
}
void praktor_regex_destroy(praktor_regex *regex) {
  if (!regex)
    return;
  pcre2_match_data_free(regex->matches);
  pcre2_match_context_free(regex->context);
  pcre2_code_free(regex->expression);
  free(regex);
}
