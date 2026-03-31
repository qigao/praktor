#pragma once

#include <string>
#include <vector>

namespace Praktor::Expressions {

enum class TokenType {
    VARIABLE,      // $var or $var.path
    STRING,        // "string" or 'string'
    NUMBER,        // 123, 123.45
    OPERATOR,      // ==, !=, &&, ||, !, <, >, <=, >=
    LPAREN,        // (
    RPAREN,        // )
    REGEX,         // /pattern/
    DOT,           // .
    IDENTIFIER,    // function names, keywords
    END
};

struct Token {
    TokenType type;
    std::string value;
    size_t position;
};

class ExpressionLexer {
public:
    explicit ExpressionLexer(const std::string& input)
        : input_(input), pos_(0) {}

    std::vector<Token> tokenize();

private:
    std::string input_;
    size_t pos_;

    char peek() const;
    char advance();
    void skipWhitespace();

    Token readVariable();
    Token readString(char quote);
    Token readNumber();
    Token readOperator();
    Token readRegex();
    Token readIdentifier();
};

} // namespace Praktor::Expressions
