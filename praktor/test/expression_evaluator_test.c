#include "expressions/expression_evaluator.h"
#include "expressions/expression_regex_salts.h"
#include <fmt.h>
#include <math.h>
#include <salts/thread.h>
#include <string.h>
#include <tinytest.h>

typedef struct fixture {
    int resolves, compiles, searches, destroys;
    int fail_compile, fail_search, empty_handle, matches;
} fixture;
static int is(vstr text, const char *value) { return vstr_eq(text, vstr_from_cstr(value)); }
static int resolve(void *user, vstr path, size_t max_bytes, praktor_expression_value *out,
    char *error, size_t capacity) {
    fixture *state = user;
    vstr text;
    ++state->resolves;
    if (is(path, "nested"))
        return praktor_expression_evaluate(vstr_from_cstr("'inner'.includes('in')"), NULL,
            NULL, out, NULL, error, capacity);
    if (is(path, "nil") || is(path, "missing")) return SALTS_OK;
    if (is(path, "flag")) { out->kind = PRAKTOR_EXPRESSION_BOOLEAN; out->as.boolean = 1; return SALTS_OK; }
    if (is(path, "age") || is(path, "nan") || is(path, "infinity") || is(path, "zero")) {
        out->kind = PRAKTOR_EXPRESSION_NUMBER;
        out->as.number = is(path, "age") ? 25 : is(path, "nan") ? NAN : is(path, "infinity") ? INFINITY : -0.0;
        return SALTS_OK;
    }
    text = vstr_from_cstr(is(path, "a") ? "1" : is(path, "b") ? "2" : is(path, "env") ? "prod" :
        is(path, "tag") ? "v1.0" : is(path, "json") ? "{\"key\":7}" : "");
    if (is(path, "binary")) text = vstr_from_buf("a\0b", 3);
    if (is(path, "bad") || is(path, "oversized")) {
        int status = praktor_expression_value_copy_string(vstr_from_cstr("owned"), 5, out);
        if (status != SALTS_OK) return status;
        if (is(path, "oversized")) return SALTS_OK; /* Deliberately violate the requested limit. */
        if (error && capacity) (void)fmt(error, capacity, "{}", "variable read failed");
        return SALTS_EIO; /* Partial owner must still be released by the core. */
    }
    return praktor_expression_value_copy_string(text, max_bytes, out);
}
/* Protocol mock only: provider behavior is covered independently below. */
static int compile(void *user, vstr pattern, void **out, char *error, size_t capacity) {
    fixture *state = user;
    ++state->compiles;
    if (state->empty_handle) return SALTS_OK;
    *out = tstr_new_len(pattern.data, pattern.len);
    if (!*out) return SALTS_ENOMEM;
    if (state->fail_compile) {
        if (error && capacity) (void)fmt(error, capacity, "{}", "compile failed");
        return SALTS_EIO;
    }
    return SALTS_OK;
}
static int search(void *user, void *regex, vstr text, int *matches, char *error, size_t capacity) {
    fixture *state = user;
    (void)regex; (void)text;
    ++state->searches;
    if (state->fail_search) {
        if (error && capacity) (void)fmt(error, capacity, "{}", "search failed");
        return SALTS_EIO;
    }
    *matches = state->matches; return SALTS_OK;
}
static void destroy(void *user, void *regex) { fixture *state = user; ++state->destroys; tstr_free(regex); }
static praktor_expression_host host(fixture *state, int regex) {
    praktor_expression_host result = {state, resolve, NULL, NULL, NULL};
    if (regex) { result.regex_compile = compile; result.regex_search = search; result.regex_destroy = destroy; }
    return result;
}
static void expect_bool(const char *expression, int expected, fixture *state) {
    praktor_expression_host hooks = host(state, 0);
    praktor_expression_value value = {0};
    char error[256]; int truthy = -1;
    int status = praktor_expression_evaluate(vstr_from_cstr(expression), &hooks, NULL, &value, &truthy, error, sizeof(error));
    info("expression: %s; status: %d; error: %s", expression, status, error);
    check(status == SALTS_OK && truthy == expected && error[0] == '\0');
    praktor_expression_value_destroy(&value);
}
static void rejected(vstr expression, const praktor_expression_host *hooks,
    const praktor_expression_limits *limits, int expected, const char *fragment) {
    praktor_expression_value value = {0}; int truthy = 42;
    char error[256];
    value.kind = PRAKTOR_EXPRESSION_NUMBER; value.as.number = 77;
    int status = praktor_expression_evaluate(expression, hooks, limits, &value, &truthy, error, sizeof(error));
    info("rejected status: %d; diagnostic: %s", status, error);
    check(status == expected && strstr(error, fragment) != NULL);
    check(value.kind == PRAKTOR_EXPRESSION_NUMBER && value.as.number > 76 && value.as.number < 78 && truthy == 42);
    praktor_expression_value_destroy(&value);
}
typedef struct parallel_fixture { fixture state; int failed; } parallel_fixture;
static void evaluate_parallel(void *user) {
    parallel_fixture *state = user;
    praktor_expression_host hooks = host(&state->state, 0);
    size_t iteration;
    for (iteration = 0; iteration < 250; ++iteration) {
        praktor_expression_value value = {0};
        int truthy = 0;
        int status = praktor_expression_evaluate(
            vstr_from_cstr("($env).startsWith('pro') && $nested && $age > 18"),
            &hooks, NULL, &value, &truthy, NULL, 0);
        if (status != SALTS_OK || truthy != 1 || value.kind != PRAKTOR_EXPRESSION_BOOLEAN)
            state->failed = 1;
        praktor_expression_value_destroy(&value);
    }
}

spec("C expression evaluator") {
    it("retains precedence, path reads and eager logical evaluation") {
        fixture state = {0};
        expect_bool("$env == 'prod' && $tag == 'v1.0'", 1, &state);
        expect_bool("($a == '1' || $b == '1') && $b == '2'", 1, &state);
        expect_bool("!($age < 18) && $age < 65", 1, &state);
        state.resolves = 0;
        expect_bool("true || $a == 1", 1, &state);
        check(state.resolves == 1);
        { praktor_expression_host hooks = host(&state, 0);
          rejected(vstr_from_cstr("false && $bad"), &hooks, NULL, SALTS_EIO, "variable read failed"); }
    }
    it("preserves numeric coercion, null, NaN and legacy numeric prefix parsing") {
        fixture state = {0};
        expect_bool("'1' == true", 1, &state);
        expect_bool("'0' == false", 1, &state);
        expect_bool("$nil == $missing", 1, &state);
        expect_bool("$nil == false", 0, &state);
        expect_bool("1..2 == 1", 1, &state);
        expect_bool("true < 2 && ' 25 ' >= 25", 1, &state);
        expect_bool("$nan == $nan", 0, &state);
        expect_bool("$infinity == $infinity", 0, &state);
        expect_bool("$nan", 0, &state);
        expect_bool("$zero", 0, &state);
        expect_bool("$flag", 1, &state);
    }
    it("executes binary-safe string methods and six-decimal number conversion") {
        fixture state = {0};
        expect_bool("'hello world'.includes('world') && 'hello'.startsWith('he')", 1, &state);
        expect_bool("'hello'.endsWith('lo')", 1, &state);
        expect_bool("(12).startsWith('12.000000')", 1, &state);
        expect_bool("true.includes('ru') && ($nil).includes('')", 1, &state);
        expect_bool("($binary).endsWith('b')", 1, &state);
        expect_bool("$binary == 'a'", 0, &state);
        expect_bool("($json).includes('key')", 1, &state);
    }
    it("returns owned typed values, including embedded NUL bytes") {
        fixture state = {0};
        praktor_expression_host hooks = host(&state, 0);
        praktor_expression_value value = {0};
        const char literal[] = {'\'', 'a', '\0', 'b', '\''};
        check(praktor_expression_evaluate(vstr_from_buf(literal, sizeof(literal)), NULL, NULL, &value, NULL, NULL, 0) == SALTS_OK);
        check(value.kind == PRAKTOR_EXPRESSION_STRING && tstr_len(value.as.string) == 3 && memcmp(value.as.string, "a\0b", 3) == 0);
        praktor_expression_value_destroy(&value);
        check(praktor_expression_evaluate(vstr_from_cstr("$age"), &hooks, NULL, &value, NULL, NULL, 0) == SALTS_OK);
        check(value.kind == PRAKTOR_EXPRESSION_NUMBER && value.as.number > 24 && value.as.number < 26);
        praktor_expression_value_destroy(&value);
        check(praktor_expression_evaluate(vstr_from_cstr("$binary"), &hooks, NULL, &value, NULL, NULL, 0) == SALTS_OK);
        check(value.kind == PRAKTOR_EXPRESSION_STRING && tstr_len(value.as.string) == 3);
        praktor_expression_value_destroy(&value);
        check(value.kind == PRAKTOR_EXPRESSION_NULL);
    }
    it("validates every token before reading variables and fails closed on syntax errors") {
        fixture state = {0}; praktor_expression_host hooks = host(&state, 0);
        const char *invalid[] = {"", "true === false", "'x'.includes()", "'x'.unknown('y')", "'x'.(true)",
            "'x'.includes true", "(true", "'x'.includes('y'", "true false", "$a < 'text'", "unknown"};
        size_t index;
        rejected(vstr_from_cstr("$a == 1 @"), &hooks, NULL, SALTS_EINVAL, "unexpected character");
        check(state.resolves == 0);
        for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index)
            rejected(vstr_from_cstr(invalid[index]), &hooks, NULL, SALTS_EINVAL, "");
    }
    it("enforces input, token, nesting and value limits without publishing results") {
        fixture state = {0}; praktor_expression_host hooks = host(&state, 0);
        praktor_expression_limits limits = praktor_expression_default_limits();
        limits.max_input_bytes = 3;
        rejected(vstr_from_cstr("true"), &hooks, &limits, SALTS_ENOBUFS, "input limit");
        limits = praktor_expression_default_limits(); limits.max_tokens = 3;
        rejected(vstr_from_cstr("true || false"), &hooks, &limits, SALTS_ENOBUFS, "token limit");
        limits = praktor_expression_default_limits(); limits.max_depth = 3;
        rejected(vstr_from_cstr("(((true)))"), &hooks, &limits, SALTS_ENOBUFS, "nesting limit");
        rejected(vstr_from_cstr("!!!true"), &hooks, &limits, SALTS_ENOBUFS, "nesting limit");
        limits = praktor_expression_default_limits(); limits.max_string_bytes = 2;
        rejected(vstr_from_cstr("'abc'"), &hooks, &limits, SALTS_ENOBUFS, "string");
        rejected(vstr_from_cstr("$oversized"), &hooks, &limits, SALTS_ENOBUFS, "string limit");
        rejected(vstr_from_cstr("(12).includes('1')"), &hooks, &limits, SALTS_ENOBUFS, "string limit");
        check(state.compiles == 0 && state.destroys == 0);
    }
    it("owns and releases regex Host handles on every success and failure path") {
        fixture state = {0}; praktor_expression_host hooks = host(&state, 1);
        praktor_expression_value value = {0}; int truthy = 0;
        check(praktor_expression_evaluate(vstr_from_cstr("/ok/"), &hooks, NULL, &value, &truthy, NULL, 0) == SALTS_OK);
        check(value.kind == PRAKTOR_EXPRESSION_NULL && truthy == 1 && state.compiles == 1 && state.destroys == 1 && state.searches == 0);
        state.matches = 1;
        check(praktor_expression_evaluate(vstr_from_cstr("/ok/.test('x')"), &hooks, NULL, &value, &truthy, NULL, 0) == SALTS_OK);
        check(value.kind == PRAKTOR_EXPRESSION_BOOLEAN && truthy == 1 && state.compiles == 2 && state.destroys == 2 && state.searches == 1);
        praktor_expression_value_destroy(&value);
        rejected(vstr_from_cstr("/ok/.test('x'"), &hooks, NULL, SALTS_EINVAL, "expected ')'");
        state.fail_compile = 1;
        rejected(vstr_from_cstr("/ok/"), &hooks, NULL, SALTS_EIO, "compile failed");
        state.fail_compile = 0; state.fail_search = 1;
        rejected(vstr_from_cstr("'ok'.test('x')"), &hooks, NULL, SALTS_EIO, "search failed");
        check(state.compiles == state.destroys);
        state.fail_search = 0; state.empty_handle = 1;
        rejected(vstr_from_cstr("/ok/"), &hooks, NULL, SALTS_EIO, "no handle");
        check(state.compiles == state.destroys + 1);
    }
    it("requires explicit Host capabilities and rejects invalid C arguments") {
        fixture state = {0}; praktor_expression_host hooks = host(&state, 0);
        praktor_expression_value value = {0};
        praktor_expression_limits limits = praktor_expression_default_limits();
        char error[2] = {'x', 'x'};
        rejected(vstr_from_cstr("true || /ok/.test('x')"), &hooks, NULL, SALTS_ENOTSUP, "regex Host");
        rejected(vstr_from_cstr("$a"), NULL, NULL, SALTS_ENOTSUP, "variable Host");
        hooks.regex_compile = compile;
        rejected(vstr_from_cstr("true"), &hooks, NULL, SALTS_EINVAL, "arguments");
        limits.max_depth = 129;
        rejected(vstr_from_cstr("true"), NULL, &limits, SALTS_EINVAL, "arguments");
        limits = praktor_expression_default_limits(); limits.max_tokens = 0;
        rejected(vstr_from_cstr("true"), NULL, &limits, SALTS_EINVAL, "arguments");
        { vstr invalid = {NULL, 1}; rejected(invalid, NULL, NULL, SALTS_EINVAL, "arguments"); }
        check(praktor_expression_evaluate(vstr_from_cstr("true"), NULL, NULL, NULL, NULL, error, sizeof(error)) == SALTS_EINVAL);
        check(error[0] != '\0' && error[1] == '\0');
        check(praktor_expression_evaluate(vstr_from_cstr("true"), NULL, NULL, &value, NULL, NULL, 1) == SALTS_EINVAL);
        check(state.compiles == 0 && state.destroys == 0);
    }
    it("supports nested Host evaluation and independent concurrent callers") {
        cmeta_thread_t threads[4] = {0};
        parallel_fixture states[4] = {0};
        size_t created = 0, index;
        int joined = 1;
        for (; created < 4; ++created)
            if (cmeta_thread_create(&threads[created], evaluate_parallel, &states[created]) != 0) break;
        for (index = 0; index < created; ++index)
            if (cmeta_thread_join(&threads[index]) != 0) joined = 0;
        check(created == 4 && joined);
        for (index = 0; index < created; ++index)
            check(!states[index].failed && states[index].state.resolves == 750);
    }
}

spec("Salts expression regex provider") {
    it("supports bounded byte matching and lookahead without C++") {
        praktor_expression_host hooks = {0};
        praktor_expression_value value = {0};
        int truthy = 0;
        char error[256];
        check(praktor_expression_regex_salts_bind(&hooks) == SALTS_OK);
        check(praktor_expression_evaluate(vstr_from_cstr("/^(ab)+$/.test('abab')"),
            &hooks, NULL, &value, &truthy, error, sizeof(error)) == SALTS_OK && truthy);
        praktor_expression_value_destroy(&value);
        check(praktor_expression_evaluate(vstr_from_cstr("/a(?=b)/.test('ab')"),
            &hooks, NULL, &value, &truthy, error, sizeof(error)) == SALTS_OK && truthy);
        praktor_expression_value_destroy(&value);
        check(praktor_expression_evaluate(vstr_from_cstr("/a(?!b)/.test('ac')"),
            &hooks, NULL, &value, &truthy, error, sizeof(error)) == SALTS_OK && truthy);
        praktor_expression_value_destroy(&value);
        check(praktor_expression_evaluate(vstr_from_cstr("/[/"), &hooks, NULL,
            &value, &truthy, error, sizeof(error)) == SALTS_EINVAL);
        check(strstr(error, "invalid pattern") != NULL);
    }
}
