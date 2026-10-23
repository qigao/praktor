#ifndef PRAKTOR_EXPRESSION_LEXER_H
#define PRAKTOR_EXPRESSION_LEXER_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum praktor_expression_token_kind {
    PRAKTOR_EXPR_VARIABLE, PRAKTOR_EXPR_STRING, PRAKTOR_EXPR_NUMBER,
    PRAKTOR_EXPR_OPERATOR, PRAKTOR_EXPR_LPAREN, PRAKTOR_EXPR_RPAREN,
    PRAKTOR_EXPR_REGEX, PRAKTOR_EXPR_DOT, PRAKTOR_EXPR_IDENTIFIER, PRAKTOR_EXPR_END
} praktor_expression_token_kind;
typedef struct praktor_expression_token {
    praktor_expression_token_kind kind;
    size_t position, size;
} praktor_expression_token;
typedef struct praktor_expression_lexer {
    const char *input;
    size_t size, offset, error_position;
    praktor_expression_token_kind previous;
    int has_previous;
} praktor_expression_lexer;
/* Borrow input until all tokens are consumed. No allocations. */
int praktor_expression_lexer_init(praktor_expression_lexer *lexer,
                                  const char *input, size_t size);
/* Return SALTS_OK / EINVAL / ENOBUFS. Buffer includes the trailing NUL, while
 * *required and token.size exclude it. Binary quoted values retain NUL bytes.
 * Output storage must not overlap the borrowed input or lexer state.
 * Too-small buffers do not consume input or change output/token. Unexpected
 * bytes set error_position and do not consume input. END is repeatable.
 * Legacy syntax is preserved: strings discard escape slashes, regex retains
 * them; unterminated quoted/regex literals reach EOF; division is tokenized
 * only when slash is followed by another byte and has a left-hand token.
 */
int praktor_expression_lexer_next(praktor_expression_lexer *lexer,
    praktor_expression_token *token, char *value, size_t capacity, size_t *required);
#ifdef __cplusplus
}
#endif
#endif
