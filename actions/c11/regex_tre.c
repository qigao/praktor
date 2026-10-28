#include "regex_tre.h"

#include <tre/tre.h>

#include <stdint.h>
#include <stdlib.h>

#define PRAKTOR_REGEX_HARD_CAPTURE_LIMIT 64u

struct praktor_regex {
  regex_t expression;
  size_t capture_count;
};

static praktor_regex_status compile_error(int error) {
  return error == REG_ESPACE ? PRAKTOR_REGEX_NOMEM
                            : PRAKTOR_REGEX_INVALID_PATTERN;
}

praktor_regex_status praktor_regex_compile(
    const char *pattern, size_t pattern_size,
    const praktor_regex_limits *limits, praktor_regex **out_regex) {
  praktor_regex *compiled;
  size_t max_captures;
  int result;
  if (out_regex == NULL) return PRAKTOR_REGEX_ENGINE;
  *out_regex = NULL;
  if (pattern == NULL || limits == NULL) return PRAKTOR_REGEX_ENGINE;
  max_captures = limits->max_capture_count;
  if (max_captures > PRAKTOR_REGEX_HARD_CAPTURE_LIMIT)
    return PRAKTOR_REGEX_LIMIT;
  compiled = (praktor_regex *)calloc(1u, sizeof(*compiled));
  if (compiled == NULL) return PRAKTOR_REGEX_NOMEM;
  result = tre_regncomp(&compiled->expression, pattern, pattern_size,
                        REG_EXTENDED);
  if (result != REG_OK) {
    free(compiled);
    return compile_error(result);
  }
  compiled->capture_count = compiled->expression.re_nsub;
  if (compiled->capture_count > max_captures) {
    tre_regfree(&compiled->expression);
    free(compiled);
    return PRAKTOR_REGEX_LIMIT;
  }
  if (tre_have_backrefs(&compiled->expression) ||
      tre_have_approx(&compiled->expression)) {
    tre_regfree(&compiled->expression);
    free(compiled);
    return PRAKTOR_REGEX_UNSUPPORTED;
  }
  *out_regex = compiled;
  return PRAKTOR_REGEX_OK;
}

size_t praktor_regex_capture_count(const praktor_regex *regex) {
  return regex == NULL ? 0u : regex->capture_count;
}

praktor_regex_status praktor_regex_search_group(
    const praktor_regex *regex, const char *input, size_t input_size,
    size_t group_index, praktor_regex_span *out_span) {
  regmatch_t matches[PRAKTOR_REGEX_HARD_CAPTURE_LIMIT + 1u];
  size_t match_count;
  int result;
  if (out_span == NULL) return PRAKTOR_REGEX_ENGINE;
  out_span->offset = 0u;
  out_span->size = 0u;
  out_span->participated = false;
  if (regex == NULL || input == NULL || group_index > regex->capture_count)
    return PRAKTOR_REGEX_ENGINE;
  match_count = regex->capture_count + 1u;
  result = tre_regnexec(&regex->expression, input, input_size, match_count,
                        matches, 0);
  if (result == REG_NOMATCH) return PRAKTOR_REGEX_NO_MATCH;
  if (result == REG_ESPACE) return PRAKTOR_REGEX_NOMEM;
  if (result != REG_OK) return PRAKTOR_REGEX_ENGINE;
  if (matches[group_index].rm_so < 0 || matches[group_index].rm_eo < 0)
    return PRAKTOR_REGEX_OK;
  if ((uintmax_t)matches[group_index].rm_so > input_size ||
      (uintmax_t)matches[group_index].rm_eo > input_size ||
      matches[group_index].rm_eo < matches[group_index].rm_so)
    return PRAKTOR_REGEX_ENGINE;
  out_span->offset = (size_t)matches[group_index].rm_so;
  out_span->size = (size_t)(matches[group_index].rm_eo -
                            matches[group_index].rm_so);
  out_span->participated = true;
  return PRAKTOR_REGEX_OK;
}

void praktor_regex_destroy(praktor_regex *regex) {
  if (regex == NULL) return;
  tre_regfree(&regex->expression);
  free(regex);
}
