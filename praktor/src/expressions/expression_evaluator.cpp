#include "expressions/expression_evaluator.hpp"

#include "expressions/expression_lexer.hpp"
#include "util/logging.hpp"

#include <cmath>
#include <cstdlib>
#include <regex>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace Praktor::Expressions {

namespace {

using EvalValue = std::variant<std::monostate, bool, double, std::string, std::regex>;

Value toPublicValue(const EvalValue& value) {
  return std::visit(
      [](const auto& item) -> Value {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return std::monostate{};
        } else if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, double> ||
                             std::is_same_v<T, std::string>) {
          return item;
        } else {
          return std::monostate{};
        }
      },
      value);
}

bool tryParseNumber(const std::string& text, double& out) {
  char* end = nullptr;
  out = std::strtod(text.c_str(), &end);
  if (end == text.c_str()) {
    return false;
  }

  while (*end != '\0') {
    if (!std::isspace(static_cast<unsigned char>(*end))) {
      return false;
    }
    ++end;
  }

  return true;
}

bool tryAsNumber(const EvalValue& value, double& out) {
  return std::visit(
      [&out](const auto& item) -> bool {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, double>) {
          out = item;
          return true;
        } else if constexpr (std::is_same_v<T, bool>) {
          out = item ? 1.0 : 0.0;
          return true;
        } else if constexpr (std::is_same_v<T, std::string>) {
          return tryParseNumber(item, out);
        } else {
          return false;
        }
      },
      value);
}

std::string toString(const EvalValue& value) {
  return std::visit(
      [](const auto& item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return {};
        } else if constexpr (std::is_same_v<T, bool>) {
          return item ? "true" : "false";
        } else if constexpr (std::is_same_v<T, double>) {
          return std::to_string(item);
        } else if constexpr (std::is_same_v<T, std::string>) {
          return item;
        } else {
          return {};
        }
      },
      value);
}

bool isTruthy(const EvalValue& value) {
  return std::visit(
      [](const auto& item) -> bool {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return false;
        } else if constexpr (std::is_same_v<T, bool>) {
          return item;
        } else if constexpr (std::is_same_v<T, double>) {
          return !std::isnan(item) && std::fpclassify(item) != FP_ZERO;
        } else if constexpr (std::is_same_v<T, std::string>) {
          return !item.empty();
        } else {
          return true;
        }
      },
      value);
}

bool valuesEqual(const EvalValue& left, const EvalValue& right) {
  double left_number = 0.0;
  double right_number = 0.0;
  if (tryAsNumber(left, left_number) && tryAsNumber(right, right_number)) {
    return std::fabs(left_number - right_number) < 1e-12;
  }

  if (std::holds_alternative<bool>(left) && std::holds_alternative<bool>(right)) {
    return std::get<bool>(left) == std::get<bool>(right);
  }

  if (std::holds_alternative<std::monostate>(left) ||
      std::holds_alternative<std::monostate>(right)) {
    return std::holds_alternative<std::monostate>(left) &&
           std::holds_alternative<std::monostate>(right);
  }

  return toString(left) == toString(right);
}

class Parser {
public:
  Parser(const std::vector<Token>& tokens, const WorkflowContext& context)
      : tokens_(tokens), context_(context) {}

  EvalValue parse() {
    EvalValue value = parseOr();
    require(TokenType::END, "unexpected trailing tokens");
    return value;
  }

private:
  const std::vector<Token>& tokens_;
  const WorkflowContext& context_;
  size_t pos_ = 0;

  const Token& peek() const { return tokens_.at(pos_); }

  const Token& previous() const { return tokens_.at(pos_ - 1); }

  bool isAtEnd() const { return peek().type == TokenType::END; }

  bool check(TokenType type) const { return peek().type == type; }

  bool checkOperator(std::string_view op) const {
    return peek().type == TokenType::OPERATOR && peek().value == op;
  }

  const Token& advance() {
    if (!isAtEnd()) {
      ++pos_;
    }
    return previous();
  }

  bool match(TokenType type) {
    if (!check(type)) {
      return false;
    }
    advance();
    return true;
  }

  bool matchOperator(std::string_view op) {
    if (!checkOperator(op)) {
      return false;
    }
    advance();
    return true;
  }

  void require(TokenType type, std::string_view message) {
    if (!match(type)) {
      throw std::runtime_error(std::string(message));
    }
  }

  EvalValue parseOr() {
    EvalValue value = parseAnd();
    while (matchOperator("||")) {
      EvalValue right = parseAnd();
      value = EvalValue(isTruthy(value) || isTruthy(right));
    }
    return value;
  }

  EvalValue parseAnd() {
    EvalValue value = parseEquality();
    while (matchOperator("&&")) {
      EvalValue right = parseEquality();
      value = EvalValue(isTruthy(value) && isTruthy(right));
    }
    return value;
  }

  EvalValue parseEquality() {
    EvalValue value = parseComparison();
    while (check(TokenType::OPERATOR)) {
      const std::string op = peek().value;
      if (op != "==" && op != "!=") {
        if (op == "===" || op == "!==") {
          throw std::runtime_error("strict equality is not supported");
        }
        break;
      }

      advance();
      EvalValue right = parseComparison();
      const bool equal = valuesEqual(value, right);
      value = EvalValue(op == "==" ? equal : !equal);
    }
    return value;
  }

  EvalValue parseComparison() {
    EvalValue value = parseUnary();
    while (check(TokenType::OPERATOR)) {
      const std::string op = peek().value;
      if (op != "<" && op != ">" && op != "<=" && op != ">=") {
        break;
      }

      advance();
      EvalValue right = parseUnary();
      double left_number = 0.0;
      double right_number = 0.0;
      if (!tryAsNumber(value, left_number) || !tryAsNumber(right, right_number)) {
        throw std::runtime_error("comparison operands must be numeric");
      }

      if (op == "<") {
        value = EvalValue(left_number < right_number);
      } else if (op == ">") {
        value = EvalValue(left_number > right_number);
      } else if (op == "<=") {
        value = EvalValue(left_number <= right_number);
      } else {
        value = EvalValue(left_number >= right_number);
      }
    }
    return value;
  }

  EvalValue parseUnary() {
    if (matchOperator("!")) {
      return EvalValue(!isTruthy(parseUnary()));
    }
    return parsePostfix();
  }

  EvalValue parsePostfix() {
    EvalValue value = parsePrimary();
    while (match(TokenType::DOT)) {
      if (!match(TokenType::IDENTIFIER)) {
        throw std::runtime_error("expected method name after '.'");
      }

      const std::string method = previous().value;
      require(TokenType::LPAREN, "expected '(' after method name");

      std::vector<EvalValue> args;
      if (!check(TokenType::RPAREN)) {
        args.push_back(parseOr());
      }

      require(TokenType::RPAREN, "expected ')' after arguments");
      value = applyMethod(value, method, args);
    }
    return value;
  }

  EvalValue parsePrimary() {
    if (match(TokenType::LPAREN)) {
      EvalValue value = parseOr();
      require(TokenType::RPAREN, "expected ')' after expression");
      return value;
    }

    if (match(TokenType::VARIABLE)) {
      return resolveVariable(previous().value);
    }

    if (match(TokenType::STRING)) {
      return EvalValue(previous().value);
    }

    if (match(TokenType::NUMBER)) {
      return EvalValue(std::stod(previous().value));
    }

    if (match(TokenType::REGEX)) {
      return EvalValue(std::regex(previous().value));
    }

    if (match(TokenType::IDENTIFIER)) {
      const std::string& value = previous().value;
      if (value == "true") {
        return EvalValue(true);
      }
      if (value == "false") {
        return EvalValue(false);
      }
      throw std::runtime_error("unknown identifier: " + value);
    }

    throw std::runtime_error("expected expression");
  }

  EvalValue resolveVariable(const std::string& path) const {
    WorkflowValue value = context_.getValueByPath(path);
    if (value.is_null()) {
      return std::monostate{};
    }
    if (value.is_bool()) {
      return value.as<bool>();
    }
    if (value.is_number()) {
      return value.as<double>();
    }
    if (value.is_string()) {
      return value.as<std::string>();
    }
    return value.to_string();
  }

  EvalValue applyMethod(const EvalValue& value, const std::string& method,
                        const std::vector<EvalValue>& args) const {
    if (method == "includes") {
      if (args.size() != 1) {
        throw std::runtime_error("includes() expects 1 argument");
      }
      return EvalValue(toString(value).find(toString(args[0])) != std::string::npos);
    }

    if (method == "startsWith") {
      if (args.size() != 1) {
        throw std::runtime_error("startsWith() expects 1 argument");
      }
      const std::string text = toString(value);
      const std::string prefix = toString(args[0]);
      return EvalValue(text.rfind(prefix, 0) == 0);
    }

    if (method == "endsWith") {
      if (args.size() != 1) {
        throw std::runtime_error("endsWith() expects 1 argument");
      }
      const std::string text = toString(value);
      const std::string suffix = toString(args[0]);
      return EvalValue(text.size() >= suffix.size() &&
                       text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0);
    }

    if (method == "test") {
      if (args.size() != 1) {
        throw std::runtime_error("test() expects 1 argument");
      }

      if (const auto* regex = std::get_if<std::regex>(&value)) {
        return EvalValue(std::regex_search(toString(args[0]), *regex));
      }

      return EvalValue(std::regex_search(toString(args[0]), std::regex(toString(value))));
    }

    throw std::runtime_error("unsupported method: " + method);
  }
};

EvalValue evaluateInternal(const std::string& expression, const WorkflowContext& context) {
  ExpressionLexer lexer(expression);
  const std::vector<Token> tokens = lexer.tokenize();
  Parser parser(tokens, context);
  return parser.parse();
}

} // namespace

bool ExpressionEvaluator::evaluateAsBool(const std::string& expression,
                                         const WorkflowContext& context) const {
  try {
    return isTruthy(evaluateInternal(expression, context));
  } catch (const std::exception& e) {
    TLOG_WARN("Expression evaluation failed for '{}': {}", expression, e.what());
    return false;
  }
}

bool ExpressionEvaluator::evaluateAsBool(const std::string& expression,
                                         const WorkflowContext& context,
                                         const FunctionRegistry& registry) const {
  (void)registry;
  return evaluateAsBool(expression, context);
}

Value ExpressionEvaluator::evaluate(const std::string& expression,
                                    const WorkflowContext& context) const {
  try {
    return toPublicValue(evaluateInternal(expression, context));
  } catch (const std::exception& e) {
    TLOG_WARN("Expression evaluation failed for '{}': {}", expression, e.what());
    return std::monostate{};
  }
}

Value ExpressionEvaluator::evaluate(const std::string& expression,
                                    const WorkflowContext& context,
                                    const FunctionRegistry& registry) const {
  (void)registry;
  return evaluate(expression, context);
}

const FunctionRegistry& ExpressionEvaluator::defaultRegistry() {
  static FunctionRegistry registry;
  return registry;
}

void FunctionRegistry::registerFunction(std::string name, FunctionSignature fn) {
  functions_[std::move(name)] = std::move(fn);
}

bool FunctionRegistry::hasFunction(const std::string& name) const noexcept {
  return functions_.find(name) != functions_.end();
}

const FunctionSignature* FunctionRegistry::findFunction(const std::string& name) const noexcept {
  auto it = functions_.find(name);
  return it != functions_.end() ? &it->second : nullptr;
}

} // namespace Praktor::Expressions
