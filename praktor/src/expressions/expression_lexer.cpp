#include "expressions/expression_lexer.hpp"
#include <cctype>
#include <stdexcept>

namespace Praktor::Expressions {

char ExpressionLexer::peek() const {
    if (pos_ >= input_.length()) return '\0';
    return input_[pos_];
}

char ExpressionLexer::advance() {
    if (pos_ >= input_.length()) return '\0';
    return input_[pos_++];
}

void ExpressionLexer::skipWhitespace() {
    while (pos_ < input_.length() && std::isspace(input_[pos_])) {
        pos_++;
    }
}

Token ExpressionLexer::readVariable() {
    size_t start = pos_;
    advance(); // skip $

    std::string value;
    while (pos_ < input_.length()) {
        char c = peek();
        if (std::isalnum(c) || c == '_' || c == '.') {
            value += advance();
        } else {
            break;
        }
    }

    return Token{TokenType::VARIABLE, value, start};
}

Token ExpressionLexer::readString(char quote) {
    size_t start = pos_;
    advance(); // skip opening quote

    std::string value;
    while (pos_ < input_.length()) {
        char c = peek();
        if (c == quote) {
            advance(); // skip closing quote
            break;
        } else if (c == '\\') {
            advance();
            if (pos_ < input_.length()) {
                value += advance();
            }
        } else {
            value += advance();
        }
    }

    return Token{TokenType::STRING, value, start};
}

Token ExpressionLexer::readNumber() {
    size_t start = pos_;
    std::string value;

    while (pos_ < input_.length()) {
        char c = peek();
        if (std::isdigit(c) || c == '.') {
            value += advance();
        } else {
            break;
        }
    }

    return Token{TokenType::NUMBER, value, start};
}

Token ExpressionLexer::readOperator() {
    size_t start = pos_;
    std::string value;

    char c = peek();
    value += advance();

    // Check for two-character operators
    if (pos_ < input_.length()) {
        char next = peek();
        if ((c == '=' && next == '=') ||
            (c == '!' && next == '=') ||
            (c == '<' && next == '=') ||
            (c == '>' && next == '=') ||
            (c == '&' && next == '&') ||
            (c == '|' && next == '|')) {
            value += advance();
        }
        // Check for === (JavaScript strict equality)
        if (c == '=' && value == "==" && peek() == '=') {
            value += advance();
        }
    }

    return Token{TokenType::OPERATOR, value, start};
}

Token ExpressionLexer::readRegex() {
    size_t start = pos_;
    advance(); // skip opening /

    std::string value;
    while (pos_ < input_.length()) {
        char c = peek();
        if (c == '/') {
            advance(); // skip closing /
            break;
        } else if (c == '\\') {
            value += advance();
            if (pos_ < input_.length()) {
                value += advance();
            }
        } else {
            value += advance();
        }
    }

    return Token{TokenType::REGEX, value, start};
}

Token ExpressionLexer::readIdentifier() {
    size_t start = pos_;
    std::string value;

    while (pos_ < input_.length()) {
        char c = peek();
        if (std::isalnum(c) || c == '_') {
            value += advance();
        } else {
            break;
        }
    }

    return Token{TokenType::IDENTIFIER, value, start};
}

std::vector<Token> ExpressionLexer::tokenize() {
    std::vector<Token> tokens;

    while (pos_ < input_.length()) {
        skipWhitespace();
        if (pos_ >= input_.length()) break;

        char c = peek();

        if (c == '$') {
            tokens.push_back(readVariable());
        } else if (c == '"' || c == '\'') {
            tokens.push_back(readString(c));
        } else if (std::isdigit(c)) {
            tokens.push_back(readNumber());
        } else if (c == '(') {
            tokens.push_back(Token{TokenType::LPAREN, "(", pos_});
            advance();
        } else if (c == ')') {
            tokens.push_back(Token{TokenType::RPAREN, ")", pos_});
            advance();
        } else if (c == '.') {
            tokens.push_back(Token{TokenType::DOT, ".", pos_});
            advance();
        } else if (c == '/' && pos_ + 1 < input_.length()) {
            // Check if it's a regex or division
            // Simple heuristic: if preceded by operator or start, it's regex
            if (tokens.empty() ||
                tokens.back().type == TokenType::OPERATOR ||
                tokens.back().type == TokenType::LPAREN) {
                tokens.push_back(readRegex());
            } else {
                tokens.push_back(readOperator());
            }
        } else if (c == '=' || c == '!' || c == '<' || c == '>' ||
                   c == '&' || c == '|') {
            tokens.push_back(readOperator());
        } else if (std::isalpha(c) || c == '_') {
            tokens.push_back(readIdentifier());
        } else {
            // Unknown character, skip it
            advance();
        }
    }

    tokens.push_back(Token{TokenType::END, "", pos_});
    return tokens;
}

} // namespace Praktor::Expressions
