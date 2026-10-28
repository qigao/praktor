#ifndef PRAKTOR_EXPRESSION_EVALUATOR_H
#define PRAKTOR_EXPRESSION_EVALUATOR_H
#include <salts/error_codes.h>
#include <tstr.h>
#include <vstr.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Internal migration API; not part of the installed Praktor SDK. Strings are
 * unique tstr owners. Initialize values to zero; destroy before replacing an
 * existing owner. Copying the struct does not clone its string. */
typedef enum praktor_expression_value_kind {
    PRAKTOR_EXPRESSION_NULL, PRAKTOR_EXPRESSION_BOOLEAN,
    PRAKTOR_EXPRESSION_NUMBER, PRAKTOR_EXPRESSION_STRING
} praktor_expression_value_kind;
typedef struct praktor_expression_value {
    praktor_expression_value_kind kind;
    union { int boolean; double number; tstr string; } as;
} praktor_expression_value;
void praktor_expression_value_destroy(praktor_expression_value *value);
/* Copy exact bytes, including embedded NUL. Failure leaves *out untouched. */
int praktor_expression_value_copy_string(vstr text, size_t max_bytes,
    praktor_expression_value *out);

typedef struct praktor_expression_limits {
    size_t max_input_bytes, max_tokens, max_depth, max_string_bytes;
} praktor_expression_limits;
/* 64 KiB input, 16384 tokens (including END), depth 128, 1 MiB strings. */
praktor_expression_limits praktor_expression_default_limits(void);

/* Host and user are borrowed until evaluation returns. Calls are synchronous,
 * on the evaluating thread. Input views are borrowed only during the call.
 * resolve transfers a value (including any partially constructed string on
 * error) to the evaluator. Missing variables resolve successfully to NULL.
 * regex_compile transfers a non-NULL opaque handle on success; any handle
 * returned even on failure is destroyed exactly once. search only borrows it.
 * All callbacks return Salts status codes and must not unwind through C.
 * The regex tuple is either entirely absent or entirely supplied. Absence is
 * ENOTSUP when regex is used, never an implicit compatibility fallback. */
typedef struct praktor_expression_host {
    void *user;
    int (*resolve)(void *user, vstr path, size_t max_string_bytes,
        praktor_expression_value *out, char *error, size_t capacity);
    int (*regex_compile)(void *user, vstr pattern, void **out,
        char *error, size_t capacity);
    int (*regex_search)(void *user, void *regex, vstr text, int *matches,
        char *error, size_t capacity);
    void (*regex_destroy)(void *user, void *regex);
} praktor_expression_host;

/* Lexically validate the complete input before calling the Host, then parse
 * with one-token lookahead. Both logical operands are evaluated, as before.
 * Limits may be NULL for defaults; max_tokens/max_depth must be positive and
 * max_depth cannot exceed 128. No mutable global state or C++ dependency.
 * Scratch is bounded by input length; strings and recursion obey the limits.
 * On success *out owns the result and optional *truthy is its truth value.
 * A regex result is publicly NULL but truthy, and its handle is already freed.
 * On failure out/truthy are untouched and all intermediate owners are freed.
 * Return OK, EINVAL (syntax/arguments), ENOBUFS (limits), ENOMEM, ENOTSUP, or a
 * Host error. error may be NULL only with capacity zero; otherwise it receives
 * a bounded terminated diagnostic. Outputs must not overlap borrowed input.
 * Host side effects are not rolled back: resolve should be a read operation. */
int praktor_expression_evaluate(vstr expression, const praktor_expression_host *host,
    const praktor_expression_limits *limits, praktor_expression_value *out,
    int *truthy, char *error, size_t error_capacity);
#ifdef __cplusplus
}
#endif
#endif
