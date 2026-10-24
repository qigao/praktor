#include "expressions/expression_evaluator.h"
#include "expressions/expression_lexer.h"
#include <ctype.h>
#include <errno.h>
#include <fmt.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { EXPRESSION_MAX_DEPTH = 128 };
typedef struct eval_value { praktor_expression_value public_value; void *regex; } eval_value;
typedef struct parser {
    praktor_expression_lexer lexer;
    praktor_expression_token token;
    tstr scratch;
    const praktor_expression_host *host;
    praktor_expression_limits limits;
    size_t depth;
    int status;
    char *error;
    size_t error_capacity;
} parser;

void praktor_expression_value_destroy(praktor_expression_value *value) {
    if (!value) return;
    if (value->kind == PRAKTOR_EXPRESSION_STRING) tstr_free(value->as.string);
    memset(value, 0, sizeof(*value));
}
int praktor_expression_value_copy_string(vstr text, size_t max_bytes,
    praktor_expression_value *out) {
    tstr copy;
    if (!out || !vstr_is_valid(text)) return SALTS_EINVAL;
    if (text.len > max_bytes || text.len == SIZE_MAX) return SALTS_ENOBUFS;
    copy = tstr_new_len(text.data, text.len);
    if (!copy) return SALTS_ENOMEM;
    memset(out, 0, sizeof(*out));
    out->kind = PRAKTOR_EXPRESSION_STRING; out->as.string = copy;
    return SALTS_OK;
}
praktor_expression_limits praktor_expression_default_limits(void) {
    praktor_expression_limits limits = {65536, 16384, EXPRESSION_MAX_DEPTH, 1048576};
    return limits;
}
static eval_value boolean_value(int value) {
    eval_value result = {0};
    result.public_value.kind = PRAKTOR_EXPRESSION_BOOLEAN;
    result.public_value.as.boolean = !!value;
    return result;
}
static int fail(parser *p, int status, const char *message) {
    if (p->status == SALTS_OK) {
        p->status = status;
        if (p->error && p->error_capacity) (void)fmt(p->error, p->error_capacity, "{}", message);
    }
    return status;
}
static void host_result(parser *p, int status) {
    if (status == SALTS_OK) return;
    p->status = status;
    if (p->error && p->error_capacity) {
        p->error[p->error_capacity - 1] = '\0';
        if (!p->error[0]) (void)fmt(p->error, p->error_capacity, "{}", "Expression Host failed");
    }
}
static void clear_value(parser *p, eval_value *value) {
    if (value->regex) p->host->regex_destroy(p->host->user, value->regex);
    praktor_expression_value_destroy(&value->public_value);
    value->regex = NULL;
}
static vstr token_text(parser *p) { return vstr_from_buf(p->scratch, p->token.size); }
static int token_is(parser *p, praktor_expression_token_kind kind, const char *text) {
    return p->status == SALTS_OK && p->token.kind == kind &&
        (!text || vstr_eq(token_text(p), vstr_from_cstr(text)));
}
static void next(parser *p) {
    size_t size = 0;
    int status;
    if (p->status != SALTS_OK) return;
    status = praktor_expression_lexer_next(&p->lexer, &p->token, p->scratch,
        tstr_len(p->scratch) + 1, &size);
    if (status != SALTS_OK) {
        fail(p, status, "Invalid expression token");
        if (status == SALTS_EINVAL && p->lexer.error_position < p->lexer.size && p->error && p->error_capacity)
            (void)fmt(p->error, p->error_capacity, "unexpected character in expression: {}",
                vstr_from_buf(p->lexer.input + p->lexer.error_position, 1));
    }
}
static int take_token(parser *p, praktor_expression_token_kind kind, const char *text) {
    if (!token_is(p, kind, text)) return 0;
    if (kind != PRAKTOR_EXPR_END) next(p);
    return 1;
}
static void require(parser *p, praktor_expression_token_kind kind, const char *message) {
    if (p->status == SALTS_OK && !take_token(p, kind, NULL)) fail(p, SALTS_EINVAL, message);
}
static int truth(const eval_value *value) {
    if (value->regex) return 1;
    switch (value->public_value.kind) {
    case PRAKTOR_EXPRESSION_NULL: return 0;
    case PRAKTOR_EXPRESSION_BOOLEAN: return !!value->public_value.as.boolean;
    case PRAKTOR_EXPRESSION_NUMBER:
        return !isnan(value->public_value.as.number) && fpclassify(value->public_value.as.number) != FP_ZERO;
    case PRAKTOR_EXPRESSION_STRING: return tstr_len(value->public_value.as.string) != 0;
    }
    return 0;
}
static int as_number(const eval_value *value, double *out) {
    char *end;
    const char *text;
    if (value->regex) return 0;
    switch (value->public_value.kind) {
    case PRAKTOR_EXPRESSION_NUMBER: *out = value->public_value.as.number; return 1;
    case PRAKTOR_EXPRESSION_BOOLEAN: *out = value->public_value.as.boolean ? 1.0 : 0.0; return 1;
    case PRAKTOR_EXPRESSION_STRING:
        text = value->public_value.as.string;
        *out = strtod(text, &end);
        if (end == text) return 0;
        while (*end) { if (!isspace((unsigned char)*end++)) return 0; }
        return 1;
    default: return 0;
    }
}
static int as_text(parser *p, const eval_value *value, vstr *out, tstr *temporary) {
    *temporary = NULL;
    *out = vstr_from_cstr("");
    if (value->regex) return SALTS_OK;
    switch (value->public_value.kind) {
    case PRAKTOR_EXPRESSION_NULL: return SALTS_OK;
    case PRAKTOR_EXPRESSION_BOOLEAN:
        *out = vstr_from_cstr(value->public_value.as.boolean ? "true" : "false"); break;
    case PRAKTOR_EXPRESSION_STRING:
        *out = vstr_from_buf(value->public_value.as.string, tstr_len(value->public_value.as.string)); break;
    case PRAKTOR_EXPRESSION_NUMBER:
        /* std::to_string(double)'s existing six-decimal conversion contract. */
        *temporary = tstr_format("{:.6f}", value->public_value.as.number);
        if (!*temporary) return fail(p, SALTS_ENOMEM, "Failed to format expression number");
        *out = vstr_from_buf(*temporary, tstr_len(*temporary)); break;
    }
    if (out->len > p->limits.max_string_bytes) return fail(p, SALTS_ENOBUFS, "Expression string limit exceeded");
    return SALTS_OK;
}
static int equal_values(parser *p, const eval_value *left, const eval_value *right) {
    double a, b;
    int left_null, right_null, equal = 0;
    vstr left_text, right_text;
    tstr left_temp = NULL, right_temp = NULL;
    if (as_number(left, &a) && as_number(right, &b)) return fabs(a - b) < 1e-12;
    left_null = !left->regex && left->public_value.kind == PRAKTOR_EXPRESSION_NULL;
    right_null = !right->regex && right->public_value.kind == PRAKTOR_EXPRESSION_NULL;
    if (left_null || right_null) return left_null && right_null;
    if (as_text(p, left, &left_text, &left_temp) == SALTS_OK &&
        as_text(p, right, &right_text, &right_temp) == SALTS_OK) equal = vstr_eq(left_text, right_text);
    tstr_free(left_temp); tstr_free(right_temp);
    return equal;
}
static int compile_regex(parser *p, vstr pattern, void **out) {
    int status;
    if (!p->host || !p->host->regex_compile) return fail(p, SALTS_ENOTSUP, "Expression regex Host is required");
    status = p->host->regex_compile(p->host->user, pattern, out, p->error, p->error_capacity);
    host_result(p, status);
    if (status == SALTS_OK && !*out) return fail(p, SALTS_EIO, "Expression regex Host returned no handle");
    return status;
}
static eval_value parse_expression(parser *p);
static eval_value parse_unary(parser *p);

static eval_value primary(parser *p) {
    eval_value value = {0};
    if (p->status != SALTS_OK) return value;
    if (take_token(p, PRAKTOR_EXPR_LPAREN, NULL)) {
        value = parse_expression(p);
        require(p, PRAKTOR_EXPR_RPAREN, "expected ')' after expression");
    } else if (token_is(p, PRAKTOR_EXPR_VARIABLE, NULL)) {
        if (!p->host || !p->host->resolve) fail(p, SALTS_ENOTSUP, "Expression variable Host is required");
        else {
            int status = p->host->resolve(p->host->user, token_text(p), p->limits.max_string_bytes,
                &value.public_value, p->error, p->error_capacity);
            host_result(p, status);
            if (p->status == SALTS_OK && (value.public_value.kind < PRAKTOR_EXPRESSION_NULL ||
                value.public_value.kind > PRAKTOR_EXPRESSION_STRING ||
                (value.public_value.kind == PRAKTOR_EXPRESSION_STRING && !value.public_value.as.string)))
                fail(p, SALTS_EIO, "Expression variable Host returned an invalid value");
            if (p->status == SALTS_OK && value.public_value.kind == PRAKTOR_EXPRESSION_STRING &&
                tstr_len(value.public_value.as.string) > p->limits.max_string_bytes)
                fail(p, SALTS_ENOBUFS, "Expression string limit exceeded");
        }
        next(p);
    } else if (token_is(p, PRAKTOR_EXPR_STRING, NULL)) {
        int status = praktor_expression_value_copy_string(token_text(p), p->limits.max_string_bytes, &value.public_value);
        if (status != SALTS_OK) fail(p, status, "Cannot copy expression string within its limit");
        next(p);
    } else if (token_is(p, PRAKTOR_EXPR_NUMBER, NULL)) {
        char *end;
        errno = 0;
        value.public_value.kind = PRAKTOR_EXPRESSION_NUMBER;
        value.public_value.as.number = strtod(p->scratch, &end);
        /* Preserve stod's prefix conversion (including legacy 1..2 -> 1),
         * and its range failure. Lexer already validates the token alphabet. */
        if (end == p->scratch || errno == ERANGE) fail(p, SALTS_EINVAL, "stod");
        next(p);
    } else if (token_is(p, PRAKTOR_EXPR_REGEX, NULL)) {
        compile_regex(p, token_text(p), &value.regex);
        next(p);
    } else if (token_is(p, PRAKTOR_EXPR_IDENTIFIER, "true") || token_is(p, PRAKTOR_EXPR_IDENTIFIER, "false")) {
        value = boolean_value(token_is(p, PRAKTOR_EXPR_IDENTIFIER, "true")); next(p);
    } else if (token_is(p, PRAKTOR_EXPR_IDENTIFIER, NULL)) {
        fail(p, SALTS_EINVAL, "unknown identifier");
        if (p->error && p->error_capacity) (void)fmt(p->error, p->error_capacity, "unknown identifier: {}", token_text(p));
    } else fail(p, SALTS_EINVAL, "expected expression");
    return value;
}
static eval_value method(parser *p, const eval_value *receiver, vstr name, const eval_value *arg, int has_arg) {
    eval_value result = {0};
    vstr text, other;
    tstr text_temp = NULL, other_temp = NULL;
    void *regex = NULL;
    int matches = 0;
    int is_includes = vstr_eq(name, vstr_from_cstr("includes"));
    int is_starts = vstr_eq(name, vstr_from_cstr("startsWith"));
    int is_ends = vstr_eq(name, vstr_from_cstr("endsWith"));
    int is_test = vstr_eq(name, vstr_from_cstr("test"));
    if (!is_includes && !is_starts && !is_ends && !is_test) {
        fail(p, SALTS_EINVAL, "unsupported method");
        if (p->error && p->error_capacity) (void)fmt(p->error, p->error_capacity, "unsupported method: {}", name);
        return result;
    }
    if (!has_arg) {
        fail(p, SALTS_EINVAL, "method expects 1 argument");
        if (p->error && p->error_capacity) (void)fmt(p->error, p->error_capacity, "{}() expects 1 argument", name);
        return result;
    }
    if (as_text(p, receiver, &text, &text_temp) != SALTS_OK || as_text(p, arg, &other, &other_temp) != SALTS_OK) goto cleanup;
    if (is_includes) matches = vstr_contains(text, other);
    else if (is_starts) matches = vstr_starts_with(text, other);
    else if (is_ends) matches = vstr_ends_with(text, other);
    else {
        void *compiled = receiver->regex;
        if (!compiled) { if (compile_regex(p, text, &regex) != SALTS_OK) goto cleanup; compiled = regex; }
        host_result(p, p->host->regex_search(p->host->user, compiled, other, &matches, p->error, p->error_capacity));
    }
    result = boolean_value(matches);
cleanup:
    if (regex) p->host->regex_destroy(p->host->user, regex);
    tstr_free(text_temp); tstr_free(other_temp);
    return result;
}
static eval_value postfix(parser *p) {
    eval_value value = primary(p);
    while (take_token(p, PRAKTOR_EXPR_DOT, NULL)) {
        tstr name;
        eval_value arg = {0}, result = {0};
        int has_arg = 0;
        if (!token_is(p, PRAKTOR_EXPR_IDENTIFIER, NULL)) { fail(p, SALTS_EINVAL, "expected method name after '.'"); break; }
        name = tstr_new_len(p->scratch, p->token.size);
        if (!name) { fail(p, SALTS_ENOMEM, "Cannot copy expression method"); break; }
        next(p);
        require(p, PRAKTOR_EXPR_LPAREN, "expected '(' after method name");
        if (p->status == SALTS_OK && !token_is(p, PRAKTOR_EXPR_RPAREN, NULL)) { has_arg = 1; arg = parse_expression(p); }
        require(p, PRAKTOR_EXPR_RPAREN, "expected ')' after arguments");
        if (p->status == SALTS_OK) result = method(p, &value, vstr_from_buf(name, tstr_len(name)), &arg, has_arg);
        clear_value(p, &arg); clear_value(p, &value); tstr_free(name); value = result;
    }
    return value;
}
static int enter(parser *p) {
    if (p->status != SALTS_OK) return 0;
    if (p->depth >= p->limits.max_depth) { fail(p, SALTS_ENOBUFS, "Expression nesting limit exceeded"); return 0; }
    ++p->depth; return 1;
}
static eval_value parse_unary(parser *p) {
    eval_value value = {0};
    if (take_token(p, PRAKTOR_EXPR_OPERATOR, "!")) {
        if (enter(p)) {
            eval_value operand = parse_unary(p);
            value = boolean_value(!truth(&operand)); clear_value(p, &operand); --p->depth;
        }
        return value;
    }
    return postfix(p);
}
static eval_value comparison(parser *p) {
    eval_value value = parse_unary(p);
    while (p->status == SALTS_OK && p->token.kind == PRAKTOR_EXPR_OPERATOR) {
        int op = token_is(p, PRAKTOR_EXPR_OPERATOR, "<") ? 1 : token_is(p, PRAKTOR_EXPR_OPERATOR, ">") ? 2 :
            token_is(p, PRAKTOR_EXPR_OPERATOR, "<=") ? 3 : token_is(p, PRAKTOR_EXPR_OPERATOR, ">=") ? 4 : 0;
        eval_value right;
        double a = 0, b = 0;
        int result = 0;
        if (!op) break;
        next(p); right = parse_unary(p);
        if (p->status == SALTS_OK) {
            if (!as_number(&value, &a) || !as_number(&right, &b)) fail(p, SALTS_EINVAL, "comparison operands must be numeric");
            else result = op == 1 ? a < b : op == 2 ? a > b : op == 3 ? a <= b : a >= b;
        }
        clear_value(p, &right); clear_value(p, &value); value = boolean_value(result);
    }
    return value;
}
static eval_value equality(parser *p) {
    eval_value value = comparison(p);
    while (p->status == SALTS_OK && p->token.kind == PRAKTOR_EXPR_OPERATOR) {
        int op = token_is(p, PRAKTOR_EXPR_OPERATOR, "==") ? 1 : token_is(p, PRAKTOR_EXPR_OPERATOR, "!=") ? 2 : 0;
        eval_value right;
        int equal = 0;
        if (!op) {
            if (token_is(p, PRAKTOR_EXPR_OPERATOR, "===") || token_is(p, PRAKTOR_EXPR_OPERATOR, "!=="))
                fail(p, SALTS_EINVAL, "strict equality is not supported");
            break;
        }
        next(p); right = comparison(p);
        if (p->status == SALTS_OK) equal = equal_values(p, &value, &right);
        clear_value(p, &right); clear_value(p, &value); value = boolean_value(op == 1 ? equal : !equal);
    }
    return value;
}
static eval_value logical(parser *p, int is_or) {
    eval_value value = is_or ? logical(p, 0) : equality(p);
    while (take_token(p, PRAKTOR_EXPR_OPERATOR, is_or ? "||" : "&&")) {
        eval_value right = is_or ? logical(p, 0) : equality(p);
        int result = is_or ? truth(&value) || truth(&right) : truth(&value) && truth(&right);
        clear_value(p, &right); clear_value(p, &value); value = boolean_value(result);
    }
    return value;
}
static eval_value parse_expression(parser *p) {
    eval_value value = {0};
    if (enter(p)) { value = logical(p, 1); --p->depth; }
    return value;
}

int praktor_expression_evaluate(vstr expression, const praktor_expression_host *host,
    const praktor_expression_limits *limits, praktor_expression_value *out,
    int *truthy, char *error, size_t error_capacity) {
    parser p = {0};
    eval_value value = {0};
    size_t tokens = 0;
    p.host = host; p.limits = limits ? *limits : praktor_expression_default_limits();
    p.error = error; p.error_capacity = error_capacity;
    if (error && error_capacity) error[0] = '\0';
    if (!vstr_is_valid(expression) || !out || (!error && error_capacity) ||
        !p.limits.max_tokens || !p.limits.max_depth || p.limits.max_depth > EXPRESSION_MAX_DEPTH ||
        (host && (host->regex_compile || host->regex_search || host->regex_destroy) &&
         (!host->regex_compile || !host->regex_search || !host->regex_destroy)))
        return fail(&p, SALTS_EINVAL, "Invalid expression evaluation arguments");
    if (expression.len > p.limits.max_input_bytes || expression.len == SIZE_MAX)
        return fail(&p, SALTS_ENOBUFS, "Expression input limit exceeded");
    p.scratch = tstr_new_len(NULL, expression.len);
    if (!p.scratch) return fail(&p, SALTS_ENOMEM, "Cannot allocate expression scratch");
    (void)praktor_expression_lexer_init(&p.lexer, expression.data, expression.len);
    do {
        if (tokens == p.limits.max_tokens) { fail(&p, SALTS_ENOBUFS, "Expression token limit exceeded"); break; }
        next(&p); ++tokens;
    } while (p.status == SALTS_OK && p.token.kind != PRAKTOR_EXPR_END);
    if (p.status == SALTS_OK) {
        (void)praktor_expression_lexer_init(&p.lexer, expression.data, expression.len);
        next(&p); value = parse_expression(&p);
        require(&p, PRAKTOR_EXPR_END, "unexpected trailing tokens");
    }
    if (p.status == SALTS_OK) {
        if (truthy) *truthy = truth(&value);
        *out = value.public_value;
        memset(&value.public_value, 0, sizeof(value.public_value));
    }
    clear_value(&p, &value); tstr_free(p.scratch);
    return p.status;
}
