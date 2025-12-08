#ifndef WEAVE_EXPRESSIONS_EXPRESSION_EVALUATOR_HPP
#define WEAVE_EXPRESSIONS_EXPRESSION_EVALUATOR_HPP

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "dag/workflow_context.hpp"

namespace Weave::Expressions {

using Value = std::variant<std::monostate, bool, double, std::string>;

using FunctionSignature = std::function<Value(const std::vector<Value>&, const WorkflowContext&)>;

class FunctionRegistry {
public:
  void registerFunction(std::string name, FunctionSignature fn);
  bool hasFunction(const std::string& name) const noexcept;
  const FunctionSignature* findFunction(const std::string& name) const noexcept;

private:
  std::unordered_map<std::string, FunctionSignature> functions_;
};

class ExpressionEvaluator {
public:
  Value evaluate(const std::string& expression,
                 const WorkflowContext& context) const;

  Value evaluate(const std::string& expression,
                 const WorkflowContext& context,
                 const FunctionRegistry& registry) const;

  bool evaluateAsBool(const std::string& expression,
                      const WorkflowContext& context) const;

  bool evaluateAsBool(const std::string& expression,
                      const WorkflowContext& context,
                      const FunctionRegistry& registry) const;

  static const FunctionRegistry& defaultRegistry();
};

}  // namespace Weave::Expressions

#endif  // WEAVE_EXPRESSIONS_EXPRESSION_EVALUATOR_HPP
