#include "actions/regex_pcre2.h"
#include <string.h>
#include <tinytest.h>
static praktor_regex *expression;
static praktor_regex_span span;
static void compile(const char *pattern) {
  const praktor_regex_limits limits = {4u};
  praktor_regex_destroy(expression);
  expression = NULL;
  check(praktor_regex_compile(pattern, strlen(pattern), &limits, &expression) == PRAKTOR_REGEX_OK);
}
spec("Bounded native PCRE2 provider") {
  before_each() {
    expression = NULL;
    span = (praktor_regex_span){0u, 0u, false};
  }
  after_each() { praktor_regex_destroy(expression); }
  it("uses native leftmost-first alternatives rather than POSIX longest selection") {
    compile("(a|ab)(c)?");
    check(praktor_regex_capture_count(expression) == 2u);
    check(praktor_regex_search_group(expression, "xxabc", 5u, 0u, &span) == PRAKTOR_REGEX_OK);
    check(span.participated && span.offset == 2u && span.size == 1u);
    check(praktor_regex_search_group(expression, "xxabc", 5u, 1u, &span) == PRAKTOR_REGEX_OK);
    check(span.participated && span.offset == 2u && span.size == 1u);
    check(praktor_regex_search_group(expression, "xxabc", 5u, 2u, &span) == PRAKTOR_REGEX_OK);
    check(!span.participated && span.offset == 0u && span.size == 0u);
  }
  it("backtracks alternatives when anchors require a complete match") {
    compile("^(a|ab)(c)?$");
    check(praktor_regex_search_group(expression, "abc", 3u, 1u, &span) == PRAKTOR_REGEX_OK);
    check(span.participated && span.offset == 0u && span.size == 2u);
    check(praktor_regex_search_group(expression, "abc", 3u, 2u, &span) == PRAKTOR_REGEX_OK);
    check(span.participated && span.offset == 2u && span.size == 1u);
  }
  it("supports native lookaround, lazy quantifiers and backreferences") {
    compile("(?<=prefix )([a-z]+?)(?=;)");
    check(praktor_regex_search_group(expression, "prefix alpha; tail", 18u, 1u, &span) ==
          PRAKTOR_REGEX_OK);
    check(span.participated && span.offset == 7u && span.size == 5u);
    compile("\\b(\\w+)\\s+\\1\\b");
    check(praktor_regex_search_group(expression, "echo echo", 9u, 1u, &span) == PRAKTOR_REGEX_OK);
    check(span.participated && span.offset == 0u && span.size == 4u);
  }
  it("clears unmatched optional captures when cached match data is reused") {
    compile("^(a)?(b)?$");
    check(praktor_regex_search_group(expression, "a", 1u, 1u, &span) == PRAKTOR_REGEX_OK);
    check(span.participated && span.size == 1u);
    check(praktor_regex_search_group(expression, "b", 1u, 1u, &span) == PRAKTOR_REGEX_OK);
    check(!span.participated && span.offset == 0u && span.size == 0u);
    check(praktor_regex_search_group(expression, "a", 1u, 2u, &span) == PRAKTOR_REGEX_OK);
    check(!span.participated && span.offset == 0u && span.size == 0u);
  }
  it("uses explicit byte lengths for patterns and subjects containing NUL") {
    const praktor_regex_limits limits = {0u};
    const char pattern[] = {'a', '\0', 'b'}, subject[] = {'x', 'a', '\0', 'b', 'y'};
    check(praktor_regex_compile(pattern, sizeof pattern, &limits, &expression) == PRAKTOR_REGEX_OK);
    check(praktor_regex_search_group(expression, subject, sizeof subject, 0u, &span) ==
          PRAKTOR_REGEX_OK);
    check(span.participated && span.offset == 1u && span.size == 3u);
  }
  it("keeps byte offsets when a pattern explicitly enables UTF") {
    const char subject[] = "\xc3\xa9";
    compile("(.)");
    check(praktor_regex_search_group(expression, subject, 2u, 1u, &span) == PRAKTOR_REGEX_OK);
    check(span.participated && span.size == 1u);
    compile("(*UTF)(.)");
    check(praktor_regex_search_group(expression, subject, 2u, 1u, &span) == PRAKTOR_REGEX_OK);
    check(span.participated && span.offset == 0u && span.size == 2u);
    check(praktor_regex_search_group(expression, "\xff", 1u, 0u, &span) == PRAKTOR_REGEX_ENGINE);
    check(!span.participated && span.offset == 0u && span.size == 0u);
  }
  it("distinguishes an empty participating match from no match or invalid input") {
    compile("");
    check(praktor_regex_search_group(expression, "", 0u, 0u, &span) == PRAKTOR_REGEX_OK);
    check(span.participated && span.offset == 0u && span.size == 0u);
    compile("a");
    check(praktor_regex_search_group(expression, "none", 4u, 0u, &span) == PRAKTOR_REGEX_NO_MATCH);
    check(!span.participated && span.size == 0u);
    check(praktor_regex_search_group(expression, "a", 1u, 1u, &span) == PRAKTOR_REGEX_ENGINE);
    check(!span.participated && span.size == 0u);
    check(praktor_regex_search_group(expression, NULL, 0u, 0u, &span) == PRAKTOR_REGEX_ENGINE);
    check(praktor_regex_search_group(expression, "a", (size_t)-1, 0u, &span) ==
          PRAKTOR_REGEX_ENGINE);
  }
  it("rejects invalid patterns and capture or compile-budget excess") {
    praktor_regex_limits limits = {1u};
    check(praktor_regex_compile("[", 1u, &limits, &expression) == PRAKTOR_REGEX_INVALID_PATTERN);
    check(expression == NULL);
    check(praktor_regex_compile("(a)(b)", 6u, &limits, &expression) == PRAKTOR_REGEX_LIMIT);
    check(expression == NULL);
    limits.max_capture_count = PRAKTOR_REGEX_HARD_CAPTURE_LIMIT + 1u;
    check(praktor_regex_compile("a", 1u, &limits, &expression) == PRAKTOR_REGEX_LIMIT);
    limits.max_capture_count = 1u;
    check(praktor_regex_compile("a", PRAKTOR_REGEX_MAX_PATTERN_BYTES + 1u, &limits, &expression) ==
          PRAKTOR_REGEX_LIMIT);
    check(expression == NULL);
    check(praktor_regex_compile(NULL, 0u, &limits, &expression) == PRAKTOR_REGEX_ENGINE);
    check(expression == NULL);
  }
  it("maps match, depth and heap exhaustion to the existing limit result") {
    const char *patterns[] = {"(*LIMIT_HEAP=0)a", "(*LIMIT_DEPTH=1)^(a+)+$",
                              "(*NO_START_OPT)(*NO_AUTO_POSSESS)(*LIMIT_MATCH=10)^(a+)+$"};
    const char *inputs[] = {"a", "aaaa", "aaaaaaaaaaaaaaaaaaaaaaaa!"};
    for (size_t i = 0u; i < 3u; ++i) {
      compile(patterns[i]);
      check(praktor_regex_search_group(expression, inputs[i], strlen(inputs[i]), 0u, &span) ==
            PRAKTOR_REGEX_LIMIT);
      check(!span.participated && span.offset == 0u && span.size == 0u);
    }
    check(praktor_regex_search_group(expression, "a", 1u, 0u, &span) == PRAKTOR_REGEX_OK);
    check(span.participated && span.size == 1u);
  }
  it("enforces the host work budget when a pattern requests a higher limit") {
    compile("(*NO_START_OPT)(*NO_AUTO_POSSESS)(*LIMIT_MATCH=1000000000)^(a+)+$");
    const char subject[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!";
    check(praktor_regex_search_group(expression, subject, strlen(subject), 0u, &span) ==
          PRAKTOR_REGEX_LIMIT);
    check(!span.participated && span.size == 0u);
  }
}
