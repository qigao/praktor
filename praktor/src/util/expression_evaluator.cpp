#include <algorithm>
#include <cctype>
#include <cmath>  // Required for std::fabs
#include <iostream>
#include <limits>  // Required for std::numeric_limits
#include <memory>
#include <stack>
#include <stdexcept>
#include <vector>

#include "util/expression_evaluator.hpp"

#include <tao/pegtl.hpp>
#include <tao/pegtl/contrib/abnf.hpp>
#include <tao/pegtl/contrib/analyze.hpp>

#include "util/logging.hpp"

namespace Praktor::Util
{

namespace pegtl = TAO_PEGTL_NAMESPACE;

// --- AST Nodes (internal implementation) ---

using LiteralValue = std::variant<std::monostate, std::string, double, bool>;

struct ASTNode
{
  virtual ~ASTNode() = default;
  virtual LiteralValue evaluate(const WorkflowContext& context) const = 0;
};

struct LiteralNode : ASTNode
{
  LiteralValue value;

  explicit LiteralNode(LiteralValue val)
      : value(std::move(val))
  {
  }

  LiteralValue evaluate(const WorkflowContext& context) const override
  {
    return value;
  }
};

struct VariableNode : ASTNode
{
  std::string name;

  explicit VariableNode(std::string var_name)
      : name(std::move(var_name))
  {
  }

  LiteralValue evaluate(const WorkflowContext& context) const override
  {
    // Trim whitespace from variable name since PEGTL might include spaces
    std::string trimmed_name = name;
    trimmed_name.erase(
        trimmed_name.begin(),
        std::find_if(trimmed_name.begin(),
                     trimmed_name.end(),
                     [](unsigned char ch) { return !std::isspace(ch); }));
    trimmed_name.erase(
        std::find_if(trimmed_name.rbegin(),
                     trimmed_name.rend(),
                     [](unsigned char ch) { return !std::isspace(ch); })
            .base(),
        trimmed_name.end());

    if (context.hasKey(trimmed_name)) {
      auto value = context.getValueOrDefault<std::string>(trimmed_name, "");
      return value;
    }
    TLOG_WARN("Expression variable '{}' not found in context, evaluating as empty string.", trimmed_name);
    return {};
  }
};

enum class BinaryOp
{
  EQ,
  NEQ,
  LT,
  GT,
  LTE,
  GTE,
  AND,
  OR
};

struct NotNode : ASTNode
{
  std::unique_ptr<ASTNode> operand;

  explicit NotNode(std::unique_ptr<ASTNode> op)
      : operand(std::move(op))
  {
  }

  LiteralValue evaluate(const WorkflowContext& context) const override;
};

struct BinaryOpNode : ASTNode
{
  BinaryOp op;
  std::unique_ptr<ASTNode> left;
  std::unique_ptr<ASTNode> right;

  BinaryOpNode(BinaryOp o,
               std::unique_ptr<ASTNode> l,
               std::unique_ptr<ASTNode> r)
      : op(o)
      , left(std::move(l))
      , right(std::move(r))
  {
  }

  LiteralValue evaluate(const WorkflowContext& context) const override;
};

// --- PEGTL Grammar (Revised for Correct Associativity) ---

namespace grammar
{
using namespace pegtl;

struct expression;

struct ws : star<space>
{
};

struct true_ : string<'t', 'r', 'u', 'e'>
{
};

struct false_ : string<'f', 'a', 'l', 's', 'e'>
{
};

struct boolean : sor<true_, false_>
{
};

struct number : seq<opt<one<'-'>>, plus<digit>, opt<seq<one<'.'>, plus<digit>>>>
{
};

// Grammar to support escaped quotes inside strings
struct escaped_quote : string<'\\', '\''>
{
};

struct string_content : star<sor<escaped_quote, not_one<'\''>>>
{
};

struct string_literal : seq<one<'\''>, string_content, one<'\''>>
{
};

struct variable_start : string<'{', '{'>
{
};

struct variable_end : string<'}', '}'>
{
};

struct variable_content : until<at<seq<ws, variable_end>>>
{
};

struct variable : seq<variable_start, ws, variable_content, ws, variable_end>
{
};

struct primary
    : sor<variable,
          number,
          string_literal,
          boolean,
          seq<one<'('>, ws, expression, ws, one<')'>>>
{
};

// Operators
struct op_eq : pad<string<'=', '='>, space>
{
};

struct op_neq : pad<string<'!', '='>, space>
{
};

struct op_lte : pad<string<'<', '='>, space>
{
};

struct op_gte : pad<string<'>', '='>, space>
{
};

struct op_lt : pad<one<'<'>, space>
{
};

struct op_gt : pad<one<'>'>, space>
{
};

struct op_not : pad<string<'n', 'o', 't'>, space>
{
};

struct op_and : pad<string<'a', 'n', 'd'>, space>
{
};

struct op_or : pad<string<'o', 'r'>, space>
{
};

// **REVISED**: Left-recursive grammar to ensure correct operator precedence and
// associativity
struct comparison_expr;
struct logical_and_expr;
struct unary_expr;

// Unary not expression
struct not_expr : seq<op_not, unary_expr>
{
};

struct unary_expr : sor<not_expr, primary>
{
};

// Comparison operators (order matters: check <= before <, >= before >)
struct op_lte_tail : seq<op_lte, unary_expr>
{
};

struct op_gte_tail : seq<op_gte, unary_expr>
{
};

struct op_lt_tail : seq<op_lt, unary_expr>
{
};

struct op_gt_tail : seq<op_gt, unary_expr>
{
};

struct op_eq_tail : seq<op_eq, unary_expr>
{
};

struct op_neq_tail : seq<op_neq, unary_expr>
{
};

struct comparison_expr : seq<unary_expr, star<sor<op_lte_tail, op_gte_tail, op_lt_tail, op_gt_tail, op_eq_tail, op_neq_tail>>>
{
};

struct op_and_tail : seq<op_and, comparison_expr>
{
};

struct logical_and_expr : seq<comparison_expr, star<op_and_tail>>
{
};

struct op_or_tail : seq<op_or, logical_and_expr>
{
};

struct expression : seq<logical_and_expr, star<op_or_tail>>
{
};

struct main : pegtl::seq<ws, expression, ws, pegtl::eof>
{
};

}  // namespace grammar

// --- PEGTL Actions to build AST ---

struct ast_builder
{
  std::stack<std::unique_ptr<ASTNode>> nodes;

  // This method is now the central point for creating binary operation nodes.
  void push_binary_op(BinaryOp op)
  {
    if (nodes.size() < 2) {
      throw std::runtime_error(
          "Syntax error: insufficient operands for operator.");
    }
    auto r = std::move(nodes.top());
    nodes.pop();
    auto l = std::move(nodes.top());
    nodes.pop();
    nodes.push(std::make_unique<BinaryOpNode>(op, std::move(l), std::move(r)));
  }

  void push_not()
  {
    if (nodes.empty()) {
      throw std::runtime_error("Syntax error: missing operand for 'not'.");
    }
    auto operand = std::move(nodes.top());
    nodes.pop();
    nodes.push(std::make_unique<NotNode>(std::move(operand)));
  }
};

template<typename Rule>
struct action : pegtl::nothing<Rule>
{
};

template<>
struct action<grammar::number>
{
  template<typename Input>
  static void apply(const Input& in, ast_builder& state)
  {
    state.nodes.push(std::make_unique<LiteralNode>(std::stod(in.string())));
  }
};

template<>
struct action<grammar::string_literal>
{
  template<typename Input>
  static void apply(const Input& in, ast_builder& state)
  {
    std::string s = in.string().substr(1, in.string().length() - 2);
    // Basic un-escaping for single quotes. A full implementation would handle
    // \n, \t, etc.
    size_t pos = s.find("\\'");
    while (pos != std::string::npos) {
      s.replace(pos, 2, "'");
      pos = s.find("\\'", pos + 1);
    }
    state.nodes.push(std::make_unique<LiteralNode>(s));
  }
};

template<>
struct action<grammar::boolean>
{
  template<typename Input>
  static void apply(const Input& in, ast_builder& state)
  {
    state.nodes.push(std::make_unique<LiteralNode>(in.string() == "true"));
  }
};

template<>
struct action<grammar::variable_content>
{
  template<typename Input>
  static void apply(const Input& in, ast_builder& state)
  {
    state.nodes.push(std::make_unique<VariableNode>(in.string()));
  }
};

// **REVISED**: Actions now apply operators as they are parsed, ensuring
// left-to-right evaluation.
template<>
struct action<grammar::op_eq_tail>
{
  template<typename Input>
  static void apply(const Input&, ast_builder& state)
  {
    state.push_binary_op(BinaryOp::EQ);
  }
};

template<>
struct action<grammar::op_neq_tail>
{
  template<typename Input>
  static void apply(const Input&, ast_builder& state)
  {
    state.push_binary_op(BinaryOp::NEQ);
  }
};

template<>
struct action<grammar::op_lt_tail>
{
  template<typename Input>
  static void apply(const Input&, ast_builder& state)
  {
    state.push_binary_op(BinaryOp::LT);
  }
};

template<>
struct action<grammar::op_gt_tail>
{
  template<typename Input>
  static void apply(const Input&, ast_builder& state)
  {
    state.push_binary_op(BinaryOp::GT);
  }
};

template<>
struct action<grammar::op_lte_tail>
{
  template<typename Input>
  static void apply(const Input&, ast_builder& state)
  {
    state.push_binary_op(BinaryOp::LTE);
  }
};

template<>
struct action<grammar::op_gte_tail>
{
  template<typename Input>
  static void apply(const Input&, ast_builder& state)
  {
    state.push_binary_op(BinaryOp::GTE);
  }
};

template<>
struct action<grammar::not_expr>
{
  template<typename Input>
  static void apply(const Input&, ast_builder& state)
  {
    state.push_not();
  }
};

template<>
struct action<grammar::op_and_tail>
{
  template<typename Input>
  static void apply(const Input&, ast_builder& state)
  {
    state.push_binary_op(BinaryOp::AND);
  }
};

template<>
struct action<grammar::op_or_tail>
{
  template<typename Input>
  static void apply(const Input&, ast_builder& state)
  {
    state.push_binary_op(BinaryOp::OR);
  }
};

// --- Evaluation Logic ---

bool to_bool(const LiteralValue& val);

// Helper to extract numeric value for comparison
std::optional<double> to_number(const LiteralValue& val)
{
  if (std::holds_alternative<double>(val)) {
    return std::get<double>(val);
  }
  if (std::holds_alternative<std::string>(val)) {
    try {
      return std::stod(std::get<std::string>(val));
    } catch (...) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

// Numeric comparison for <, >, <=, >=
int compare_numeric(const LiteralValue& lhs, const LiteralValue& rhs)
{
  auto l = to_number(lhs);
  auto r = to_number(rhs);
  if (!l || !r) {
    throw std::runtime_error("Cannot compare non-numeric values with <, >, <=, >=");
  }
  constexpr double eps = 1e-9;
  if (std::fabs(*l - *r) < eps) return 0;
  return (*l < *r) ? -1 : 1;
}

// **IMPROVED**: Uses an epsilon for safe floating-point comparison.
bool compare(const LiteralValue& lhs, const LiteralValue& rhs)
{
  if (std::holds_alternative<std::string>(lhs)
      && std::holds_alternative<std::string>(rhs))
  {
    return std::get<std::string>(lhs) == std::get<std::string>(rhs);
  }
  if (std::holds_alternative<double>(lhs)
      && std::holds_alternative<double>(rhs))
  {
    return std::fabs(std::get<double>(lhs) - std::get<double>(rhs))
        < std::numeric_limits<double>::epsilon();
  }
  if (std::holds_alternative<bool>(lhs) && std::holds_alternative<bool>(rhs)) {
    return std::get<bool>(lhs) == std::get<bool>(rhs);
  }
  if (std::holds_alternative<double>(lhs)
      && std::holds_alternative<std::string>(rhs))
  {
    try {
      return std::fabs(std::get<double>(lhs)
                       - std::stod(std::get<std::string>(rhs)))
          < std::numeric_limits<double>::epsilon();
    } catch (const std::invalid_argument&) {
      return false;
    }
  }
  if (std::holds_alternative<std::string>(lhs)
      && std::holds_alternative<double>(rhs))
  {
    try {
      return std::fabs(std::stod(std::get<std::string>(lhs))
                       - std::get<double>(rhs))
          < std::numeric_limits<double>::epsilon();
    } catch (const std::invalid_argument&) {
      return false;
    }
  }
  // Type mismatch comparison is always false.
  return false;
}

LiteralValue BinaryOpNode::evaluate(const WorkflowContext& context) const
{
  LiteralValue left_val = left->evaluate(context);

  // Short-circuit evaluation for logical operators
  if (op == BinaryOp::AND) {
    if (!to_bool(left_val)) {
      return false;
    }
    return to_bool(right->evaluate(context));
  }
  if (op == BinaryOp::OR) {
    if (to_bool(left_val)) {
      return true;
    }
    return to_bool(right->evaluate(context));
  }

  LiteralValue right_val = right->evaluate(context);
  if (op == BinaryOp::EQ) {
    return compare(left_val, right_val);
  }
  if (op == BinaryOp::NEQ) {
    return !compare(left_val, right_val);
  }
  if (op == BinaryOp::LT) {
    return compare_numeric(left_val, right_val) < 0;
  }
  if (op == BinaryOp::GT) {
    return compare_numeric(left_val, right_val) > 0;
  }
  if (op == BinaryOp::LTE) {
    return compare_numeric(left_val, right_val) <= 0;
  }
  if (op == BinaryOp::GTE) {
    return compare_numeric(left_val, right_val) >= 0;
  }

  return false;  // Should be unreachable
}

LiteralValue NotNode::evaluate(const WorkflowContext& context) const
{
  return !to_bool(operand->evaluate(context));
}

bool to_bool(const LiteralValue& val)
{
  if (std::holds_alternative<bool>(val)) {
    return std::get<bool>(val);
  }
  if (std::holds_alternative<std::string>(val)) {
    return !std::get<std::string>(val).empty();
  }
  if (std::holds_alternative<double>(val)) {
    return std::get<double>(val) != 0.0;
  }
  if (std::holds_alternative<std::monostate>(val)) {
    return false;
  }
  return false;
}

// --- ExpressionEvaluator Implementation ---

bool ExpressionEvaluator::evaluate(const std::string& expression,
                                   const WorkflowContext& context)
{
  if (expression.empty()) {
    return true;
  }

  // Pre-check for invalid operators before parsing
  if (expression.find("===") != std::string::npos) {
    throw std::runtime_error("Invalid operator '===' in expression: "
                             + expression);
  }
  if (expression.find("!==") != std::string::npos) {
    throw std::runtime_error("Invalid operator '!==' in expression: "
                             + expression);
  }
  // Check for incomplete expressions (missing quotes, etc.)
  size_t single_quote_count =
      std::count(expression.begin(), expression.end(), '\'');
  if (single_quote_count % 2 != 0) {
    throw std::runtime_error("Unmatched single quote in expression: "
                             + expression);
  }
  // Check for invalid patterns like "variable and literal" without comparison
  if (expression.find("}} and '") != std::string::npos
      || expression.find("}} or '") != std::string::npos)
  {
    throw std::runtime_error(
        "Invalid expression: logical operators require boolean operands, not "
        "string comparisons: "
        + expression);
  }

  ast_builder state;
  pegtl::string_input in(expression, "expression");
  try {
    pegtl::parse<grammar::main, action>(in, state);
  } catch (const pegtl::parse_error& e) {
    throw std::runtime_error("Expression parsing failed: "
                             + std::string(e.what()));
  }

  if (state.nodes.empty()) {
    // This can happen for a valid expression that is just whitespace.
    return true;
  }

  if (state.nodes.size() != 1) {
    // This check is the final guard against malformed expressions that the
    // grammar might miss.
    throw std::runtime_error(
        "Invalid expression syntax: malformed operator/operand sequence.");
  }

  auto root = std::move(state.nodes.top());
  return to_bool(root->evaluate(context));
}

}  // namespace Praktor::Util
