#include "expressions/expression_lexer.h"
#include <salts/error_codes.h>
#include <string.h>
#include <tinytest.h>

static void expect(praktor_expression_lexer *lexer, praktor_expression_token_kind kind,
    const char *value, size_t size, size_t position) {
    char buffer[128]; size_t required = 0;
    praktor_expression_token token = {0};
    check(praktor_expression_lexer_next(lexer, &token, buffer, sizeof(buffer), &required) == SALTS_OK);
    check(token.kind == kind && token.position == position && token.size == size && required == size);
    check(memcmp(buffer, value, size) == 0 && buffer[size] == '\0');
}
spec("C expression lexer") {
    it("retains variable, operator, method, string and regex token semantics") {
        const char text[] = " $task.result == 'a\\n' && /a\\/b/.test($v)";
        praktor_expression_lexer lexer;
        check(praktor_expression_lexer_init(&lexer, text, sizeof(text) - 1) == SALTS_OK);
        expect(&lexer, PRAKTOR_EXPR_VARIABLE, "task.result", 11, 1);
        expect(&lexer, PRAKTOR_EXPR_OPERATOR, "==", 2, 14);
        expect(&lexer, PRAKTOR_EXPR_STRING, "an", 2, 17);
        expect(&lexer, PRAKTOR_EXPR_OPERATOR, "&&", 2, 23);
        expect(&lexer, PRAKTOR_EXPR_REGEX, "a\\/b", 4, 26);
        expect(&lexer, PRAKTOR_EXPR_DOT, ".", 1, 32);
        expect(&lexer, PRAKTOR_EXPR_IDENTIFIER, "test", 4, 33);
        expect(&lexer, PRAKTOR_EXPR_LPAREN, "(", 1, 37);
        expect(&lexer, PRAKTOR_EXPR_VARIABLE, "v", 1, 38);
        expect(&lexer, PRAKTOR_EXPR_RPAREN, ")", 1, 40);
        expect(&lexer, PRAKTOR_EXPR_END, "", 0, 41);
        expect(&lexer, PRAKTOR_EXPR_END, "", 0, 41);
    }
    it("preserves legacy strict operators, number text and division heuristics") {
        praktor_expression_lexer lexer;
        const char text[] = "123.45 === 1..2 !== 9 / x";
        check(praktor_expression_lexer_init(&lexer, text, sizeof(text) - 1) == SALTS_OK);
        expect(&lexer, PRAKTOR_EXPR_NUMBER, "123.45", 6, 0);
        expect(&lexer, PRAKTOR_EXPR_OPERATOR, "===", 3, 7);
        expect(&lexer, PRAKTOR_EXPR_NUMBER, "1..2", 4, 11);
        expect(&lexer, PRAKTOR_EXPR_OPERATOR, "!=", 2, 16);
        expect(&lexer, PRAKTOR_EXPR_OPERATOR, "=", 1, 18);
        expect(&lexer, PRAKTOR_EXPR_NUMBER, "9", 1, 20);
        expect(&lexer, PRAKTOR_EXPR_OPERATOR, "/", 1, 22);
        expect(&lexer, PRAKTOR_EXPR_IDENTIFIER, "x", 1, 24);
        expect(&lexer, PRAKTOR_EXPR_END, "", 0, 25);
    }
    it("decodes binary strings and legacy unterminated literals") {
        const char binary[] = {'\'', 'a', '\0', 'b', '\''};
        praktor_expression_lexer lexer;
        check(praktor_expression_lexer_init(&lexer, binary, sizeof(binary)) == SALTS_OK);
        expect(&lexer, PRAKTOR_EXPR_STRING, "a\0b", 3, 0);
        check(praktor_expression_lexer_init(&lexer, "'tail\\", 6) == SALTS_OK);
        expect(&lexer, PRAKTOR_EXPR_STRING, "tail", 4, 0);
        check(praktor_expression_lexer_init(&lexer, "/tail\\", 6) == SALTS_OK);
        expect(&lexer, PRAKTOR_EXPR_REGEX, "tail\\", 5, 0);
        check(praktor_expression_lexer_init(&lexer, "'unclosed", 9) == SALTS_OK);
        expect(&lexer, PRAKTOR_EXPR_STRING, "unclosed", 8, 0);
        check(praktor_expression_lexer_init(&lexer, "$", 1) == SALTS_OK);
        expect(&lexer, PRAKTOR_EXPR_VARIABLE, "", 0, 0);
    }
    it("retries capacity failures without advancing input or touching outputs") {
        praktor_expression_lexer lexer;
        praktor_expression_token token = {PRAKTOR_EXPR_RPAREN, 42, 42};
        char output[8] = "old"; size_t required = 0;
        check(praktor_expression_lexer_init(&lexer, "  'abc'", 7) == SALTS_OK);
        check(praktor_expression_lexer_next(&lexer, &token, output, 3, &required) == SALTS_ENOBUFS);
        check(lexer.offset == 0 && !lexer.has_previous && required == 3);
        check(token.kind == PRAKTOR_EXPR_RPAREN && token.position == 42 && strcmp(output, "old") == 0);
        check(praktor_expression_lexer_next(&lexer, &token, NULL, 0, &required) == SALTS_ENOBUFS);
        expect(&lexer, PRAKTOR_EXPR_STRING, "abc", 3, 2);
        check(praktor_expression_lexer_init(&lexer, "  @", 3) == SALTS_OK);
        check(praktor_expression_lexer_next(&lexer, &token, output, sizeof(output), &required) == SALTS_EINVAL);
        check(lexer.offset == 0 && lexer.error_position == 2);
        check(praktor_expression_lexer_init(&lexer, "/", 1) == SALTS_OK);
        check(praktor_expression_lexer_next(&lexer, &token, output, sizeof(output), &required) == SALTS_EINVAL);
        check(praktor_expression_lexer_init(&lexer, NULL, 1) == SALTS_EINVAL);
        check(praktor_expression_lexer_init(&lexer, NULL, 0) == SALTS_OK);
        expect(&lexer, PRAKTOR_EXPR_END, "", 0, 0);
    }
}
