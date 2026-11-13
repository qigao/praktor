#include "regex_tre.h"

#include <tinytest.h>

#include <string.h>

static void compile_and_capture(void) {
  const praktor_regex_limits limits = {4u};
  praktor_regex *regex = NULL;
  praktor_regex_span span = {0u, 0u, false};
  const char input[] = "xxabc yyab";
  check(praktor_regex_compile("(a|ab)(c)?", 10u, &limits, &regex) ==
        PRAKTOR_REGEX_OK);
  check(regex != NULL && praktor_regex_capture_count(regex) == 2u);
  check(praktor_regex_search_group(regex, input, strlen(input), 0u, &span) ==
        PRAKTOR_REGEX_OK);
  check(span.participated && span.offset == 2u && span.size == 3u);
  check(praktor_regex_search_group(regex, input, strlen(input), 1u, &span) ==
        PRAKTOR_REGEX_OK);
  check(span.participated && span.offset == 2u && span.size == 2u);
  check(praktor_regex_search_group(regex, "ab", 2u, 2u, &span) ==
        PRAKTOR_REGEX_OK);
  check(!span.participated && span.size == 0u);
  check(praktor_regex_search_group(regex, "none", 4u, 0u, &span) ==
        PRAKTOR_REGEX_NO_MATCH);
  praktor_regex_destroy(regex);
}

static void reject_unbounded_features(void) {
  const praktor_regex_limits one_capture = {1u};
  const praktor_regex_limits four_captures = {4u};
  praktor_regex *regex = NULL;
  check(praktor_regex_compile("[", 1u, &four_captures, &regex) ==
        PRAKTOR_REGEX_INVALID_PATTERN);
  check(regex == NULL);
  check(praktor_regex_compile("(a)(b)", 6u, &one_capture, &regex) ==
        PRAKTOR_REGEX_LIMIT);
  check(regex == NULL);
  check(praktor_regex_compile("(a)\\1", 5u, &four_captures, &regex) ==
        PRAKTOR_REGEX_UNSUPPORTED);
  check(regex == NULL);
}

spec("Bounded TRE regex provider") {
  it("returns POSIX leftmost-longest captures without search allocation") {
    compile_and_capture();
  }
  it("rejects invalid, oversized, and backtracking expressions") {
    reject_unbounded_features();
  }
}
