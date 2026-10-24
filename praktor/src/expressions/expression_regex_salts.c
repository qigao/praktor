#include "expressions/expression_regex_salts.h"

#include <re.h>

#include <stdio.h>

static int salts_status(re_status_t status) {
    switch (status) {
    case RE_STATUS_OK:
    case RE_STATUS_NO_MATCH: return SALTS_OK;
    case RE_STATUS_INVALID_ARGUMENT:
    case RE_STATUS_INVALID_PATTERN: return SALTS_EINVAL;
    case RE_STATUS_PATTERN_LIMIT:
    case RE_STATUS_TEXT_LIMIT:
    case RE_STATUS_DEPTH_LIMIT:
    case RE_STATUS_STEP_LIMIT:
    case RE_STATUS_WORKSPACE_LIMIT: return SALTS_ENOBUFS;
    case RE_STATUS_NO_MEMORY: return SALTS_ENOMEM;
    }
    return SALTS_EIO;
}

static int fail(re_status_t status, char *error, size_t capacity) {
    const int result = salts_status(status);
    if (result != SALTS_OK && error != NULL && capacity != 0u)
        (void)snprintf(error, capacity, "Expression regex: %s",
                       re_status_string(status));
    return result;
}

static int compile(void *user, vstr pattern, void **out,
                   char *error, size_t capacity) {
    re_t compiled = NULL;
    re_status_t status;
    (void)user;
    if (out == NULL || !vstr_is_valid(pattern) ||
        (error == NULL && capacity != 0u))
        return SALTS_EINVAL;
    *out = NULL;
    status = re_compile_n(pattern.data, pattern.len, NULL, &compiled);
    if (status != RE_STATUS_OK) {
        re_destroy(compiled);
        return fail(status, error, capacity);
    }
    *out = compiled;
    return SALTS_OK;
}

static int search(void *user, void *regex, vstr text, int *matches,
                  char *error, size_t capacity) {
    re_match_result_t match = {0u, 0u};
    re_status_t status;
    (void)user;
    if (regex == NULL || matches == NULL || !vstr_is_valid(text) ||
        (error == NULL && capacity != 0u))
        return SALTS_EINVAL;
    status = re_matchn((re_t)regex, text.data, text.len, NULL, &match);
    if (status == RE_STATUS_OK) {
        *matches = 1;
        return SALTS_OK;
    }
    if (status == RE_STATUS_NO_MATCH) {
        *matches = 0;
        return SALTS_OK;
    }
    return fail(status, error, capacity);
}

static void destroy(void *user, void *regex) {
    (void)user;
    re_destroy((re_t)regex);
}

int praktor_expression_regex_salts_bind(praktor_expression_host *host) {
    if (host == NULL) return SALTS_EINVAL;
    host->regex_compile = compile;
    host->regex_search = search;
    host->regex_destroy = destroy;
    return SALTS_OK;
}
