#include "expressions/expression_evaluator.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <tao/pegtl.hpp>
#include <tao/pegtl/contrib/parse_tree.hpp>

#include "util/logging.hpp"
#include <regex>

namespace Praktor::Expressions {

namespace pegtl = tao::pegtl;

namespace {

constexpr double kEpsilon = 1e-9;

template <typename... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};

template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

std::string trim(std::string_view text) {
  auto begin = text.begin();
  auto end = text.end();
  while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
    ++begin;
  }
  while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }
  return std::string(begin, end);
}

namespace grammar {

struct ws : pegtl::space {};

template <typename Rule> using padded = pegtl::pad<Rule, ws>;

struct identifier_start : pegtl::sor<pegtl::alpha, pegtl::one<'_'>> {};
struct identifier_char : pegtl::sor<identifier_start, pegtl::digit> {};
struct identifier : pegtl::seq<identifier_start, pegtl::star<identifier_char>> {};
struct dotted_identifier : pegtl::list<identifier, pegtl::one<'.'>> {};

struct moustache_body : pegtl::star<pegtl::not_one<'}'>> {};
struct moustache_inner : pegtl::pad<moustache_body, ws> {};
struct moustache_variable
    : pegtl::if_must<pegtl::string<'{', '{'>, moustache_inner, pegtl::string<'}', '}'>> {};

struct string_escape : pegtl::seq<pegtl::one<'\\'>, pegtl::one<'\\', '\'', '"', 'n', 'r', 't'>> {};
struct string_char : pegtl::sor<string_escape, pegtl::not_one<'\''>> {};
struct string_literal : pegtl::seq<pegtl::one<'\''>, pegtl::star<string_char>, pegtl::one<'\''>> {};

struct boolean_true : pegtl::keyword<'t', 'r', 'u', 'e'> {};
struct boolean_false : pegtl::keyword<'f', 'a', 'l', 's', 'e'> {};
struct boolean_literal : pegtl::sor<boolean_true, boolean_false> {};

struct number_literal
    : pegtl::seq<pegtl::opt<pegtl::one<'-'>>, pegtl::plus<pegtl::digit>,
                 pegtl::opt<pegtl::seq<pegtl::one<'.'>, pegtl::plus<pegtl::digit>>>> {};

struct lparen : pegtl::one<'('> {};
struct rparen : pegtl::one<')'> {};
struct comma : pegtl::one<','> {};

struct op_plus : pegtl::one<'+'> {};
struct op_minus : pegtl::one<'-'> {};
struct op_mul : pegtl::one<'*'> {};
struct op_div : pegtl::one<'/'> {};
struct op_mod : pegtl::one<'%'> {};
struct op_eq : pegtl::string<'=', '='> {};
struct op_neq : pegtl::string<'!', '='> {};
struct op_lte : pegtl::string<'<', '='> {};
struct op_gte : pegtl::string<'>', '='> {};
struct op_lt : pegtl::one<'<'> {};
struct op_gt : pegtl::one<'>'> {};
struct op_contains : pegtl::keyword<'c', 'o', 'n', 't', 'a', 'i', 'n', 's'> {};
struct op_startswith : pegtl::sor<
  pegtl::keyword<'s', 't', 'a', 'r', 't', 's', '_', 'w', 'i', 't', 'h'>,
  pegtl::keyword<'s', 't', 'a', 'r', 't', 's', 'w', 'i', 't', 'h'>
> {};
struct op_endswith : pegtl::sor<
  pegtl::keyword<'e', 'n', 'd', 's', '_', 'w', 'i', 't', 'h'>,
  pegtl::keyword<'e', 'n', 'd', 's', 'w', 'i', 't', 'h'>
> {};
struct op_in : pegtl::keyword<'i', 'n'> {};
struct op_matches : pegtl::keyword<'m', 'a', 't', 'c', 'h', 'e', 's'> {};
struct op_and : pegtl::keyword<'a', 'n', 'd'> {};
struct op_or : pegtl::keyword<'o', 'r'> {};
struct op_not : pegtl::keyword<'n', 'o', 't'> {};
struct op_bang : pegtl::one<'!'> {};

struct expression;
struct argument_list_tail;
struct argument_list;

struct function_name : identifier {};
struct function_call
    : pegtl::seq<function_name, padded<lparen>, pegtl::opt<argument_list>, padded<rparen>> {};

struct bare_variable : dotted_identifier {};

struct leaf : pegtl::sor<function_call, moustache_variable, string_literal, number_literal,
                         boolean_literal, bare_variable> {};

struct paren_expression : pegtl::seq<padded<lparen>, expression, padded<rparen>> {};
struct primary : pegtl::sor<paren_expression, padded<leaf>> {};

struct unary_prefix : pegtl::sor<op_not, op_bang, op_minus> {};
struct unary : pegtl::seq<pegtl::star<padded<unary_prefix>>, primary> {};

struct multiplicative_tail : pegtl::seq<padded<pegtl::sor<op_mul, op_div, op_mod>>, unary> {};
struct multiplicative : pegtl::seq<unary, pegtl::star<multiplicative_tail>> {};

struct additive_tail : pegtl::seq<padded<pegtl::sor<op_plus, op_minus>>, multiplicative> {};
struct additive : pegtl::seq<multiplicative, pegtl::star<additive_tail>> {};

struct relational_tail
    : pegtl::seq<padded<pegtl::sor<op_lte, op_gte, op_lt, op_gt, op_contains, op_startswith,
                                   op_endswith, op_in, op_matches>>,
                 additive> {};
struct relational : pegtl::seq<additive, pegtl::star<relational_tail>> {};

struct equality_tail : pegtl::seq<padded<pegtl::sor<op_eq, op_neq>>, relational> {};
struct equality : pegtl::seq<relational, pegtl::star<equality_tail>> {};

struct logical_and_tail : pegtl::seq<padded<op_and>, equality> {};
struct logical_and : pegtl::seq<equality, pegtl::star<logical_and_tail>> {};

struct logical_or_tail : pegtl::seq<padded<op_or>, logical_and> {};
struct logical_or : pegtl::seq<logical_and, pegtl::star<logical_or_tail>> {};

struct expression : pegtl::seq<logical_or> {};

struct argument_list_tail : pegtl::seq<padded<comma>, expression> {};
struct argument_list : pegtl::seq<expression, pegtl::star<argument_list_tail>> {};

struct grammar : pegtl::seq<pegtl::star<ws>, expression, pegtl::star<ws>, pegtl::eof> {};

} // namespace grammar

template <typename Rule>
using Selector = pegtl::parse_tree::selector<
    Rule,
    pegtl::parse_tree::store_content::on<
        grammar::logical_or, grammar::logical_or_tail, grammar::logical_and,
        grammar::logical_and_tail, grammar::equality, grammar::equality_tail, grammar::relational,
        grammar::relational_tail, grammar::additive, grammar::additive_tail,
        grammar::multiplicative, grammar::multiplicative_tail, grammar::unary,
        grammar::function_call, grammar::argument_list, grammar::argument_list_tail,
        grammar::boolean_literal, grammar::number_literal, grammar::string_literal,
        grammar::function_name, grammar::bare_variable, grammar::moustache_body,
        grammar::moustache_inner, grammar::op_not, grammar::op_bang, grammar::op_and,
        grammar::op_or, grammar::op_eq, grammar::op_neq, grammar::op_lte, grammar::op_gte,
        grammar::op_lt, grammar::op_gt, grammar::op_contains, grammar::op_startswith,
        grammar::op_endswith, grammar::op_in, grammar::op_matches, grammar::op_plus,
        grammar::op_minus, grammar::op_mul, grammar::op_div, grammar::op_mod>,
    pegtl::parse_tree::fold_one::on<grammar::grammar, grammar::expression,
                                    grammar::paren_expression, grammar::primary, grammar::leaf,
                                    grammar::moustache_variable>>;

const pegtl::parse_tree::node &unwrap(const pegtl::parse_tree::node &node) {
  const pegtl::parse_tree::node *current = &node;
  while (!current->has_content() && current->children.size() == 1) {
    current = current->children.front().get();
  }
  return *current;
}

std::optional<std::string> findToken(const pegtl::parse_tree::node &node) {
  if (node.has_content()) {
    return node.string();
  }
  for (const auto &child : node.children) {
    if (auto token = findToken(*child)) {
      return token;
    }
  }
  return std::nullopt;
}

// Unified type conversion - "Good taste eliminates repetition"
template<typename T>
T convert(const Value& value);

template<>
std::optional<double> convert<std::optional<double>>(const Value &value) {
  return std::visit(Overloaded{
    [](std::monostate) -> std::optional<double> { return std::nullopt; },
    [](bool b) -> std::optional<double> { return b ? 1.0 : 0.0; },
    [](double d) -> std::optional<double> { return d; },
    [](const std::string &text) -> std::optional<double> {
      if (text.empty()) return std::nullopt;
      try {
        size_t pos = 0;
        double number = std::stod(text, &pos);
        return (pos == text.size()) ? std::optional<double>(number) : std::nullopt;
      } catch (...) {
        return std::nullopt;
      }
    }
  }, value);
}

template<>
std::string convert<std::string>(const Value &value) {
  return std::visit(Overloaded{
    [](std::monostate) { return std::string{}; },
    [](bool b) { return b ? std::string("true") : std::string("false"); },
    [](double d) {
      std::ostringstream oss;
      oss << d;
      return oss.str();
    },
    [](const std::string &s) { return s; }
  }, value);
}

template<>
bool convert<bool>(const Value &value) {
  bool res = std::visit(Overloaded{
    [](std::monostate) { return false; },
    [](bool b) { return b; },
    [](double d) { return std::fabs(d) > kEpsilon; },
    [](const std::string &s) {
      if (s.empty()) return false;
      std::string lowered = s;
      std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      return !(lowered == "false" || lowered == "0" || lowered == "null");
    }
  }, value);
  logd("Expression to_bool: '{:s}' -> {:d}", convert<std::string>(value).c_str(), res);
  return res;
}

// Convenience functions for backward compatibility
std::optional<double> to_number(const Value &value) { return convert<std::optional<double>>(value); }
std::string to_string_value(const Value &value) { return convert<std::string>(value); }
bool to_bool(const Value &value) { return convert<bool>(value); }

bool equals(const Value &lhs, const Value &rhs) {
  if (lhs.index() == rhs.index()) {
    return lhs == rhs;
  }
  auto lhs_num = to_number(lhs);
  auto rhs_num = to_number(rhs);
  if (lhs_num && rhs_num) {
    return std::fabs(*lhs_num - *rhs_num) <= kEpsilon;
  }
  return to_string_value(lhs) == to_string_value(rhs);
}

int compare(const Value &lhs, const Value &rhs) {
  auto lhs_num = to_number(lhs);
  auto rhs_num = to_number(rhs);
  if (lhs_num && rhs_num) {
    if (*lhs_num < *rhs_num - kEpsilon) {
      return -1;
    }
    if (*lhs_num > *rhs_num + kEpsilon) {
      return 1;
    }
    return 0;
  }
  const auto left = to_string_value(lhs);
  const auto right = to_string_value(rhs);
  if (left < right) {
    return -1;
  }
  if (left > right) {
    return 1;
  }
  return 0;
}

Value add(const Value &lhs, const Value &rhs) {
  auto lhs_num = to_number(lhs);
  auto rhs_num = to_number(rhs);
  if (lhs_num && rhs_num) {
    return Value{*lhs_num + *rhs_num};
  }
  return Value{to_string_value(lhs) + to_string_value(rhs)};
}

Value subtract(const Value &lhs, const Value &rhs) {
  auto lhs_num = to_number(lhs);
  auto rhs_num = to_number(rhs);
  if (!lhs_num || !rhs_num) {
    throw std::runtime_error("Subtraction requires numeric operands");
  }
  return Value{*lhs_num - *rhs_num};
}

Value multiply(const Value &lhs, const Value &rhs) {
  auto lhs_num = to_number(lhs);
  auto rhs_num = to_number(rhs);
  if (!lhs_num || !rhs_num) {
    throw std::runtime_error("Multiplication requires numeric operands");
  }
  return Value{*lhs_num * *rhs_num};
}

Value divide(const Value &lhs, const Value &rhs) {
  auto lhs_num = to_number(lhs);
  auto rhs_num = to_number(rhs);
  if (!lhs_num || !rhs_num) {
    throw std::runtime_error("Division requires numeric operands");
  }
  if (std::fabs(*rhs_num) <= kEpsilon) {
    throw std::runtime_error("Division by zero");
  }
  return Value{*lhs_num / *rhs_num};
}

Value modulo(const Value &lhs, const Value &rhs) {
  auto lhs_num = to_number(lhs);
  auto rhs_num = to_number(rhs);
  if (!lhs_num || !rhs_num) {
    throw std::runtime_error("Modulo requires numeric operands");
  }
  if (std::fabs(*rhs_num) <= kEpsilon) {
    throw std::runtime_error("Modulo by zero");
  }
  return Value{std::fmod(*lhs_num, *rhs_num)};
}

std::size_t value_length(const Value &value) {
  return std::visit(
      Overloaded{[](std::monostate) -> std::size_t { return 0; },
                 [](bool b) -> std::size_t { return b ? 1 : 0; },
                 [](double d) -> std::size_t { return std::fabs(d) > kEpsilon ? 1 : 0; },
                 [](const std::string &s) -> std::size_t { return s.size(); }},
      value);
}

bool value_empty(const Value &value) {
  return std::visit(Overloaded{[](std::monostate) { return true; }, [](bool b) { return !b; },
                               [](double d) { return std::fabs(d) <= kEpsilon; },
                               [](const std::string &s) { return s.empty(); }},
                    value);
}

std::string parse_string_literal(const std::string &token) {
  std::string result;
  if (token.size() < 2) {
    return result;
  }
  result.reserve(token.size() - 2);
  for (std::size_t i = 1; i + 1 < token.size(); ++i) {
    char ch = token[i];
    if (ch == '\\' && i + 1 < token.size() - 1) {
      char next = token[++i];
      switch (next) {
      case '\\':
        result.push_back('\\');
        break;
      case '\'':
        result.push_back('\'');
        break;
      case '"':
        result.push_back('"');
        break;
      case 'n':
        result.push_back('\n');
        break;
      case 'r':
        result.push_back('\r');
        break;
      case 't':
        result.push_back('\t');
        break;
      default:
        result.push_back(next);
        break;
      }
    } else {
      result.push_back(ch);
    }
  }
  return result;
}

double parse_number_literal(const std::string &token) { return std::stod(token); }

bool parse_boolean_literal(const std::string &token) { return token == "true"; }

Value interpret_text_value(const std::string &text) {
  const auto trimmed = trim(text);
  if (trimmed.empty())
    return Value{std::string{}};

  std::string lowered = trimmed;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  if (lowered == "true")
    return Value{true};
  if (lowered == "false")
    return Value{false};
  if (lowered == "null")
    return Value{};

  try {
    size_t pos = 0;
    double number = std::stod(trimmed, &pos);
    if (pos == trimmed.size())
      return Value{number};
  } catch (...) {
  }

  return Value{trimmed};
}

Value resolve_variable(const WorkflowContext &context, const std::string &raw_name) {
  std::string name = trim(raw_name);
  if (name.empty()) {
    logi("resolve_variable: empty name, returning monostate");
    return Value{};
  }

  WorkflowValue val = context.getValueByPath(name);

  // Check for null, empty object, or empty array - these all mean "not found"
  bool is_empty =
      val.is_null() || (val.is_object() && val.empty()) || (val.is_array() && val.empty());

  if (!is_empty) {
    if (val.is_bool())
      return Value{val.as_bool()};
    if (val.is_double())
      return Value{val.as_double()};
    if (val.is_int64())
      return Value{static_cast<double>(val.as<int64_t>())};
    if (val.is_string())
      return interpret_text_value(val.as_string());
    return Value{val.to_string()};
  }

  // Only use getVariable fallback if it returns a non-empty string
  // Note: getVariable returns "" for non-existent variables, but we need to
  // distinguish between "variable exists with empty value" vs "variable doesn't exist"
  // Since getValueByPath already returned empty, the variable truly doesn't exist
  logi("Expression variable '{:s}' not found in context, returning monostate", name.c_str());
  return Value{};
}

enum class BinaryOp {
  LogicalOr,
  LogicalAnd,
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Add,
  Subtract,
  Multiply,
  Divide,
  Modulo,
  Contains,
  StartsWith,
  EndsWith,
  In,
  Matches
};

enum class UnaryOp { LogicalNot, Negate };

// Operator mapping tables - defined before use
static const std::unordered_map<std::string, UnaryOp> unary_ops = {
  {"not", UnaryOp::LogicalNot},
  {"!", UnaryOp::LogicalNot},
  {"-", UnaryOp::Negate}
};

std::optional<UnaryOp> parse_unary_operator(const pegtl::parse_tree::node &node) {
  if (auto token = findToken(node)) {
    auto content = trim(*token);
    auto it = unary_ops.find(content);
    if (it != unary_ops.end()) {
      return it->second;
    }
  }
  for (const auto &child : node.children) {
    if (auto op = parse_unary_operator(*child)) {
      return op;
    }
  }
  return std::nullopt;
}

// Unified operator parser - "Good taste eliminates repetition"
template<typename OpType>
OpType parse_operator(const pegtl::parse_tree::node &node, const std::unordered_map<std::string, OpType>& op_map) {
  if (auto token = findToken(node)) {
    auto content = trim(*token);
    auto it = op_map.find(content);
    if (it != op_map.end()) {
      return it->second;
    }
  }
  throw std::runtime_error("Unknown operator: " + (findToken(node).value_or("???")));
}

// Operator mapping tables
static const std::unordered_map<std::string, BinaryOp> equality_ops = {
  {"==", BinaryOp::Equal},
  {"!=", BinaryOp::NotEqual}
};

static const std::unordered_map<std::string, BinaryOp> relational_ops = {
  {"<=", BinaryOp::LessEqual},
  {"<", BinaryOp::Less},
  {">=", BinaryOp::GreaterEqual},
  {">", BinaryOp::Greater},
  {"contains", BinaryOp::Contains},
  {"starts_with", BinaryOp::StartsWith},
  {"startswith", BinaryOp::StartsWith},
  {"ends_with", BinaryOp::EndsWith},
  {"endswith", BinaryOp::EndsWith},
  {"in", BinaryOp::In},
  {"matches", BinaryOp::Matches}
};

static const std::unordered_map<std::string, BinaryOp> additive_ops = {
  {"+", BinaryOp::Add},
  {"-", BinaryOp::Subtract}
};

static const std::unordered_map<std::string, BinaryOp> multiplicative_ops = {
  {"*", BinaryOp::Multiply},
  {"/", BinaryOp::Divide},
  {"%", BinaryOp::Modulo}
};

// Simplified operator parsers
BinaryOp parse_equality_operator(const pegtl::parse_tree::node &node) {
  return parse_operator(node, equality_ops);
}

BinaryOp parse_relational_operator(const pegtl::parse_tree::node &node) {
  return parse_operator(node, relational_ops);
}

BinaryOp parse_additive_operator(const pegtl::parse_tree::node &node) {
  return parse_operator(node, additive_ops);
}

BinaryOp parse_multiplicative_operator(const pegtl::parse_tree::node &node) {
  return parse_operator(node, multiplicative_ops);
}

class ASTNode {
public:
  virtual ~ASTNode() = default;
  virtual Value evaluate(const WorkflowContext &context,
                         const FunctionRegistry &registry) const = 0;
};

class LiteralNode final : public ASTNode {
public:
  explicit LiteralNode(Value value) : value_(std::move(value)) {}

  Value evaluate(const WorkflowContext &, const FunctionRegistry &) const override {
    return value_;
  }

private:
  Value value_;
};

class VariableNode final : public ASTNode {
public:
  explicit VariableNode(std::string name) : name_(std::move(name)) {}

  Value evaluate(const WorkflowContext &context, const FunctionRegistry &) const override {
    return resolve_variable(context, name_);
  }

private:
  std::string name_;
};

class UnaryNode final : public ASTNode {
public:
  UnaryNode(UnaryOp op, std::unique_ptr<ASTNode> operand) : op_(op), operand_(std::move(operand)) {}

  Value evaluate(const WorkflowContext &context, const FunctionRegistry &registry) const override {
    Value value = operand_->evaluate(context, registry);
    switch (op_) {
    case UnaryOp::LogicalNot:
      return Value{!to_bool(value)};
    case UnaryOp::Negate: {
      auto number = to_number(value);
      if (!number) {
        throw std::runtime_error("Unary '-' expects a numeric operand");
      }
      return Value{-*number};
    }
    }
    return Value{};
  }

private:
  UnaryOp op_;
  std::unique_ptr<ASTNode> operand_;
};

class BinaryNode final : public ASTNode {
public:
  BinaryNode(BinaryOp op, std::unique_ptr<ASTNode> lhs, std::unique_ptr<ASTNode> rhs)
      : op_(op), lhs_(std::move(lhs)), rhs_(std::move(rhs)) {}

  Value evaluate(const WorkflowContext &context, const FunctionRegistry &registry) const override {
    if (op_ == BinaryOp::LogicalOr) {
      Value left = lhs_->evaluate(context, registry);
      if (to_bool(left))
        return Value{true};
      Value right = rhs_->evaluate(context, registry);
      return Value{to_bool(right)};
    }

    if (op_ == BinaryOp::LogicalAnd) {
      Value left = lhs_->evaluate(context, registry);
      if (!to_bool(left))
        return Value{false};
      Value right = rhs_->evaluate(context, registry);
      return Value{to_bool(right)};
    }

    Value left = lhs_->evaluate(context, registry);
    Value right = rhs_->evaluate(context, registry);

    switch (op_) {
    case BinaryOp::Equal:
      return Value{equals(left, right)};
    case BinaryOp::NotEqual:
      return Value{!equals(left, right)};
    case BinaryOp::Less:
      return Value{compare(left, right) < 0};
    case BinaryOp::LessEqual:
      return Value{compare(left, right) <= 0};
    case BinaryOp::Greater:
      return Value{compare(left, right) > 0};
    case BinaryOp::GreaterEqual:
      return Value{compare(left, right) >= 0};
    case BinaryOp::Add:
      return add(left, right);
    case BinaryOp::Subtract:
      return subtract(left, right);
    case BinaryOp::Multiply:
      return multiply(left, right);
    case BinaryOp::Divide:
      return divide(left, right);
    case BinaryOp::Modulo:
      return modulo(left, right);
    case BinaryOp::Contains: {
      std::string l = to_string_value(left);
      std::string r = to_string_value(right);
      return Value{l.find(r) != std::string::npos};
    }
    case BinaryOp::StartsWith: {
      std::string l = to_string_value(left);
      std::string r = to_string_value(right);
      return Value{l.rfind(r, 0) == 0};
    }
    case BinaryOp::EndsWith: {
      std::string l = to_string_value(left);
      std::string r = to_string_value(right);
      if (r.length() > l.length())
        return Value{false};
      return Value{l.compare(l.length() - r.length(), r.length(), r) == 0};
    }
    case BinaryOp::In: {
      // 'in' currently only works with strings (substring) or we can extend it to lists later
      std::string needle = to_string_value(left);
      std::string haystack = to_string_value(right);
      return Value{haystack.find(needle) != std::string::npos};
    }
    case BinaryOp::Matches: {
      std::string text = to_string_value(left);
      std::string pattern = to_string_value(right);
      try {
        std::regex re(pattern);
        return Value{std::regex_search(text, re)};
      } catch (...) {
        return Value{false};
      }
    }
    default:
      break;
    }
    return Value{};
  }

private:
  BinaryOp op_;
  std::unique_ptr<ASTNode> lhs_;
  std::unique_ptr<ASTNode> rhs_;
};

class FunctionCallNode final : public ASTNode {
public:
  FunctionCallNode(std::string name, std::vector<std::unique_ptr<ASTNode>> args)
      : name_(std::move(name)), args_(std::move(args)) {}

  Value evaluate(const WorkflowContext &context, const FunctionRegistry &registry) const override {
    std::vector<Value> evaluated;
    evaluated.reserve(args_.size());
    for (const auto &arg : args_) {
      evaluated.emplace_back(arg->evaluate(context, registry));
    }

    if (const auto *fn = registry.findFunction(name_)) {
      return (*fn)(evaluated, context);
    }
    if (const auto *fn = ExpressionEvaluator::defaultRegistry().findFunction(name_)) {
      return (*fn)(evaluated, context);
    }

    throw std::runtime_error("Unknown function '" + name_ + "'");
  }

private:
  std::string name_;
  std::vector<std::unique_ptr<ASTNode>> args_;
};

std::unique_ptr<ASTNode> build_node(const pegtl::parse_tree::node &node);

std::vector<std::unique_ptr<ASTNode>> build_argument_list(const pegtl::parse_tree::node &node) {
  std::vector<std::unique_ptr<ASTNode>> result;
  if (node.children.empty()) {
    return result;
  }
  const auto &first = unwrap(*node.children.front());
  result.emplace_back(build_node(first));
  for (std::size_t i = 1; i < node.children.size(); ++i) {
    const auto &tail = unwrap(*node.children[i]);
    if (tail.children.size() < 2) {
      continue;
    }
    const auto &expr = unwrap(*tail.children.back());
    result.emplace_back(build_node(expr));
  }
  return result;
}

std::unique_ptr<ASTNode> build_function_call(const pegtl::parse_tree::node &node) {
  std::string name;
  std::vector<std::unique_ptr<ASTNode>> args;
  for (const auto &child_ptr : node.children) {
    const auto &child = unwrap(*child_ptr);
    if (child.is_type<grammar::function_name>()) {
      name = child.string();
    } else if (child.is_type<grammar::argument_list>()) {
      args = build_argument_list(child);
    }
  }
  return std::make_unique<FunctionCallNode>(std::move(name), std::move(args));
}

std::unique_ptr<ASTNode> build_unary(const pegtl::parse_tree::node &node) {
  std::vector<UnaryOp> ops;
  for (std::size_t i = 0; i + 1 < node.children.size(); ++i) {
    const auto &prefix = unwrap(*node.children[i]);
    if (auto op = parse_unary_operator(prefix)) {
      ops.push_back(*op);
    }
  }
  const auto &operand = unwrap(*node.children.back());
  auto current = build_node(operand);
  for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
    current = std::make_unique<UnaryNode>(*it, std::move(current));
  }
  return current;
}

// Simplified binary sequence builder - "Good taste eliminates special cases"
std::unique_ptr<ASTNode>
build_binary_sequence(const pegtl::parse_tree::node &node,
                      BinaryOp (*operator_selector)(const pegtl::parse_tree::node &)) {
  if (node.children.empty()) {
    return std::make_unique<LiteralNode>(Value{});
  }

  auto result = build_node(unwrap(*node.children.front()));

  for (std::size_t i = 1; i < node.children.size(); ++i) {
    const auto &tail = *node.children[i];

    // Simple case: tail has operator and RHS
    if (tail.children.size() >= 2) {
      BinaryOp op = operator_selector(*tail.children.front());
      auto rhs = build_node(unwrap(*tail.children.back()));
      result = std::make_unique<BinaryNode>(op, std::move(result), std::move(rhs));
    }
    // Edge case: single child (RHS only, operator inferred)
    else if (tail.children.size() == 1) {
      BinaryOp op = operator_selector(tail);
      auto rhs = build_node(unwrap(*tail.children.front()));
      result = std::make_unique<BinaryNode>(op, std::move(result), std::move(rhs));
    }
    // Skip malformed tails instead of logging
  }
  return result;
}

// Simplified AST node builder - "Good taste groups similar cases"
std::unique_ptr<ASTNode> build_node(const pegtl::parse_tree::node &raw) {
  const auto &node = unwrap(raw);

  // Binary operators - grouped by similar handling
  if (node.is_type<grammar::logical_or>()) {
    return build_binary_sequence(node, [](const pegtl::parse_tree::node &) { return BinaryOp::LogicalOr; });
  }
  if (node.is_type<grammar::logical_and>()) {
    return build_binary_sequence(node, [](const pegtl::parse_tree::node &) { return BinaryOp::LogicalAnd; });
  }
  if (node.is_type<grammar::equality>()) {
    return build_binary_sequence(node, parse_equality_operator);
  }
  if (node.is_type<grammar::relational>()) {
    return build_binary_sequence(node, parse_relational_operator);
  }
  if (node.is_type<grammar::additive>()) {
    return build_binary_sequence(node, parse_additive_operator);
  }
  if (node.is_type<grammar::multiplicative>()) {
    return build_binary_sequence(node, parse_multiplicative_operator);
  }

  // Unary and structural nodes
  if (node.is_type<grammar::unary>()) {
    return build_unary(node);
  }
  if (node.is_type<grammar::paren_expression>()) {
    if (node.children.empty())
      throw std::runtime_error("Empty paren expression");
    return build_node(*node.children.front());
  }
  if (node.is_type<grammar::function_call>()) {
    return build_function_call(node);
  }

  // Literals - create directly without intermediate functions
  if (node.is_type<grammar::string_literal>()) {
    return std::make_unique<LiteralNode>(Value{parse_string_literal(node.string())});
  }
  if (node.is_type<grammar::number_literal>()) {
    return std::make_unique<LiteralNode>(Value{parse_number_literal(node.string())});
  }
  if (node.is_type<grammar::boolean_literal>()) {
    return std::make_unique<LiteralNode>(Value{parse_boolean_literal(node.string())});
  }

  // Variables - all variable types handled the same way
  if (node.is_type<grammar::moustache_inner>() ||
      node.is_type<grammar::moustache_body>() ||
      node.is_type<grammar::bare_variable>()) {
    return std::make_unique<VariableNode>(node.string());
  }

  // Fallback: try children
  for (const auto &child : node.children) {
    if (auto res = build_node(*child))
      return res;
  }

  throw std::runtime_error("Cannot build AST node for expression component: " +
                           (node.has_content() ? node.string() : "[sequence]"));
}

} // namespace

void FunctionRegistry::registerFunction(std::string name, FunctionSignature fn) {
  functions_.insert_or_assign(std::move(name), std::move(fn));
}

bool FunctionRegistry::hasFunction(const std::string &name) const noexcept {
  return functions_.find(name) != functions_.end();
}

const FunctionSignature *FunctionRegistry::findFunction(const std::string &name) const noexcept {
  auto it = functions_.find(name);
  if (it == functions_.end()) {
    return nullptr;
  }
  return &it->second;
}

Value ExpressionEvaluator::evaluate(const std::string &expression,
                                    const WorkflowContext &context) const {
  return evaluate(expression, context, defaultRegistry());
}

Value ExpressionEvaluator::evaluate(const std::string &expression, const WorkflowContext &context,
                                    const FunctionRegistry &registry) const {
  if (trim(expression).empty()) {
    return Value{};
  }

  pegtl::string_input in(expression, "PraktorExpression");
  // Use grammar::grammar which includes EOF to ensure the entire expression is consumed
  auto root = pegtl::parse_tree::parse<grammar::grammar, Selector>(in);
  if (!root) {
    throw std::runtime_error("Failed to parse expression: " + expression);
  }

  if (root->children.empty()) {
    return Value{};
  }

  // Log the root structure recursively
  std::function<void(const pegtl::parse_tree::node &, int)> log_tree;
  log_tree = [&log_tree](const pegtl::parse_tree::node &n, int depth) {
    for (const auto &c : n.children) {
      log_tree(*c, depth + 1);
    }
  };
  logd("evaluate: Parse tree for '{:s}':", expression.c_str());
  log_tree(*root, 0);

  // The root node should have the top-level expression as its only child (if folded)
  // or a sequence of children. We build the first one that yields an AST.
  for (const auto &child : root->children) {
    if (auto ast = build_node(*child)) {
      Value val = ast->evaluate(context, registry);
      logd("Expression final result: '{:s}'", to_string_value(val).c_str());
      return val;
    }
  }

  throw std::runtime_error("Failed to build AST for expression: " + expression);
}

bool ExpressionEvaluator::evaluateAsBool(const std::string &expression,
                                         const WorkflowContext &context) const {
  return evaluateAsBool(expression, context, defaultRegistry());
}

bool ExpressionEvaluator::evaluateAsBool(const std::string &expression,
                                         const WorkflowContext &context,
                                         const FunctionRegistry &registry) const {
  Value result = evaluate(expression, context, registry);
  bool truthy = to_bool(result);
  return truthy;
}

const FunctionRegistry &ExpressionEvaluator::defaultRegistry() {
  static FunctionRegistry registry = [] {
    FunctionRegistry reg;

    reg.registerFunction("len",
                         [](const std::vector<Value> &args, const WorkflowContext &) -> Value {
                           if (args.empty()) {
                             return Value{0.0};
                           }
                           return Value{static_cast<double>(value_length(args.front()))};
                         });

    reg.registerFunction("empty",
                         [](const std::vector<Value> &args, const WorkflowContext &) -> Value {
                           if (args.empty()) {
                             return Value{true};
                           }
                           return Value{value_empty(args.front())};
                         });

    reg.registerFunction("contains",
                         [](const std::vector<Value> &args, const WorkflowContext &) -> Value {
                           if (args.size() < 2) {
                             return Value{false};
                           }
                           const auto haystack = to_string_value(args[0]);
                           const auto needle = to_string_value(args[1]);
                           return Value{haystack.find(needle) != std::string::npos};
                         });

    reg.registerFunction(
        "lower", [](const std::vector<Value> &args, const WorkflowContext &) -> Value {
          if (args.empty()) {
            return Value{std::string{}};
          }
          std::string text = to_string_value(args.front());
          std::transform(text.begin(), text.end(), text.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
          return Value{text};
        });

    reg.registerFunction(
        "upper", [](const std::vector<Value> &args, const WorkflowContext &) -> Value {
          if (args.empty()) {
            return Value{std::string{}};
          }
          std::string text = to_string_value(args.front());
          std::transform(text.begin(), text.end(), text.begin(),
                         [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
          return Value{text};
        });

    reg.registerFunction("abs",
                         [](const std::vector<Value> &args, const WorkflowContext &) -> Value {
                           if (args.empty()) {
                             return Value{0.0};
                           }
                           auto number = to_number(args.front());
                           if (!number) {
                             throw std::runtime_error("abs() expects a numeric argument");
                           }
                           return Value{std::fabs(*number)};
                         });

    reg.registerFunction("bool",
                         [](const std::vector<Value> &args, const WorkflowContext &) -> Value {
                           if (args.empty()) {
                             return Value{false};
                           }
                           return Value{to_bool(args.front())};
                         });

    return reg;
  }();

  return registry;
}

} // namespace Praktor::Expressions
