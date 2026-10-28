#include "expressions/expression_lexer.h"
#include <salts/error_codes.h>
#include <ctype.h>
#include <string.h>

int praktor_expression_lexer_init(praktor_expression_lexer *lexer, const char *input, size_t size) {
    if (!lexer || (!input && size)) return SALTS_EINVAL;
    memset(lexer, 0, sizeof(*lexer)); lexer->input = input; lexer->size = size;
    return SALTS_OK;
}
static int word(unsigned char ch) { return isalnum(ch) || ch == '_'; }
int praktor_expression_lexer_next(praktor_expression_lexer *lexer,
    praktor_expression_token *token, char *value, size_t capacity, size_t *required) {
    praktor_expression_token next;
    size_t start, begin, end, cursor, written = 0;
    unsigned char ch;
    int quoted = 0;
    if (!lexer || !token || !required || (!value && capacity) ||
        (!lexer->input && lexer->size) || lexer->offset > lexer->size) return SALTS_EINVAL;
    start = lexer->offset;
    while (start < lexer->size && isspace((unsigned char)lexer->input[start])) ++start;
    next.position = start; begin = start; end = start; next.kind = PRAKTOR_EXPR_END;
    if (start < lexer->size) {
        ch = (unsigned char)lexer->input[start]; end = start + 1;
        if (ch == '$') {
            next.kind = PRAKTOR_EXPR_VARIABLE; begin = end;
            while (end < lexer->size && (word((unsigned char)lexer->input[end]) || lexer->input[end] == '.')) ++end;
        } else if (ch == '\'' || ch == '"' ||
            (ch == '/' && end < lexer->size && (!lexer->has_previous ||
             lexer->previous == PRAKTOR_EXPR_OPERATOR || lexer->previous == PRAKTOR_EXPR_LPAREN))) {
            next.kind = ch == '/' ? PRAKTOR_EXPR_REGEX : PRAKTOR_EXPR_STRING;
            quoted = 1; begin = end;
            while (end < lexer->size && (unsigned char)lexer->input[end] != ch) {
                if (lexer->input[end] == '\\') {
                    ++end;
                    if (next.kind == PRAKTOR_EXPR_REGEX) ++written;
                    if (end < lexer->size) { ++end; ++written; }
                } else { ++end; ++written; }
            }
            if (end < lexer->size) ++end;
        } else if (isdigit(ch)) {
            next.kind = PRAKTOR_EXPR_NUMBER;
            while (end < lexer->size && (isdigit((unsigned char)lexer->input[end]) || lexer->input[end] == '.')) ++end;
        } else if (ch == '(') next.kind = PRAKTOR_EXPR_LPAREN;
        else if (ch == ')') next.kind = PRAKTOR_EXPR_RPAREN;
        else if (ch == '.') next.kind = PRAKTOR_EXPR_DOT;
        else if (ch == '=' || ch == '!' || ch == '<' || ch == '>' || ch == '&' || ch == '|' ||
                 (ch == '/' && end < lexer->size)) {
            next.kind = PRAKTOR_EXPR_OPERATOR;
            if (end < lexer->size &&
                (((ch == '=' || ch == '!' || ch == '<' || ch == '>') && lexer->input[end] == '=') ||
                 (ch == '&' && lexer->input[end] == '&') || (ch == '|' && lexer->input[end] == '|'))) {
                ++end;
                if (ch == '=' && end < lexer->size && lexer->input[end] == '=') ++end;
            }
        } else if (isalpha(ch) || ch == '_') {
            next.kind = PRAKTOR_EXPR_IDENTIFIER;
            while (end < lexer->size && word((unsigned char)lexer->input[end])) ++end;
        } else { lexer->error_position = start; return SALTS_EINVAL; }
    }
    if (!quoted) written = end - begin;
    *required = written;
    if (capacity <= written || !value) return SALTS_ENOBUFS;
    next.size = written;
    if (!quoted) { if (written) memcpy(value, lexer->input + begin, written); }
    else {
        written = 0; cursor = begin;
        while (cursor < end && (unsigned char)lexer->input[cursor] != (unsigned char)lexer->input[start]) {
            if (lexer->input[cursor] == '\\') {
                if (next.kind == PRAKTOR_EXPR_REGEX) value[written++] = '\\';
                ++cursor;
                if (cursor < end) value[written++] = lexer->input[cursor++];
            } else value[written++] = lexer->input[cursor++];
        }
    }
    value[written] = '\0'; *token = next;
    lexer->offset = end; lexer->previous = next.kind; lexer->has_previous = 1;
    return SALTS_OK;
}
