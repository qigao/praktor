#include "expressions/expression_evaluator.hpp"
#include "expressions/expression_lexer.hpp"
#include "turbo_script.h"
#include "util/logging.hpp"
#include <sstream>
#include <stdexcept>
#include <vector>

namespace Praktor::Expressions {

namespace {

// RAII wrapper for TurboScript context
class TurboScriptContext {
public:
  TurboScriptContext() : ctx_(turbo_script_init(TURBO_SCRIPT_INIT_DEFAULT)) {
    if (!ctx_) {
      throw std::runtime_error("Failed to initialize TurboScript context");
    }
  }
  ~TurboScriptContext() { turbo_script_free(ctx_); }

  TurboScriptContext(const TurboScriptContext &) = delete;
  TurboScriptContext &operator=(const TurboScriptContext &) = delete;

  turbo_script_ctx_t *get() const { return ctx_; }

private:
  turbo_script_ctx_t *ctx_;
};

void bindWorkflowValue(turbo_script_ctx_t *ctx, const std::string &key,
                       const WorkflowValue &value) {
  if (value.is_bool()) {
    ts_bind_num(ctx, key.c_str(), value.as<bool>() ? 1.0 : 0.0);
  } else if (value.is_number()) {
    ts_bind_num(ctx, key.c_str(), value.as<double>());
  } else if (value.is_string()) {
    ts_bind_str(ctx, key.c_str(), value.as<std::string>().c_str());
  }
}

std::string convertTokensToScript(const std::vector<Token> &tokens) {
  std::ostringstream oss;

  for (const auto &token : tokens) {
    switch (token.type) {
    case TokenType::VARIABLE:
      oss << token.value;
      break;
    case TokenType::STRING:
      oss << '"' << token.value << '"';
      break;
    case TokenType::NUMBER:
      oss << token.value;
      break;
    case TokenType::LPAREN:
      oss << '(';
      break;
    case TokenType::RPAREN:
      oss << ')';
      break;
    case TokenType::DOT:
      oss << '.';
      break;
    case TokenType::IDENTIFIER:
      oss << token.value;
      break;
    case TokenType::OPERATOR:
      if (token.value == "===")
        oss << "==";
      else if (token.value == "!==")
        oss << "!=";
      else
        oss << token.value;
      break;
    case TokenType::REGEX:
      // Regex not supported, treat as string
      oss << '"' << token.value << '"';
      break;
    case TokenType::END:
      break;
    }
  }

  return oss.str();
}

} // anonymous namespace

bool ExpressionEvaluator::evaluateAsBool(const std::string &expression,
                                         const WorkflowContext &context) const {
  try {
    TurboScriptContext ts_ctx;
    auto *ctx = ts_ctx.get();

    // Bind all visible variables with proper types
    auto all_vars = context.getAllVisibleValues();
    for (const auto &[key, value] : all_vars) {
      bindWorkflowValue(ctx, key, value);
    }

    // Tokenize and convert to TurboScript syntax
    ExpressionLexer lexer(expression);
    std::vector<Token> tokens = lexer.tokenize();
    std::string script = convertTokensToScript(tokens);

    // Bind boolean literals
    ts_bind_num(ctx, "true", 1.0);
    ts_bind_num(ctx, "false", 0.0);
    ts_bind_num(ctx, "result", 0.0);

    // Execute wrapped expression
    std::string wrapped = "result = (" + script + ")";
    TLOG_DEBUG("Evaluating: {}", wrapped);

    int status = turbo_script_run(ctx, wrapped.c_str());
    if (status != 0) {
      TLOG_ERROR("Expression evaluation failed: {}", turbo_script_get_error(ctx));
      return false;
    }

    return ts_get_num(ctx, "result") > 0.5;
  } catch (const std::exception &e) {
    TLOG_ERROR("Expression evaluation error: {}", e.what());
    return false;
  }
}

bool ExpressionEvaluator::evaluateAsBool(const std::string &expression,
                                         const WorkflowContext &context,
                                         const FunctionRegistry &registry) const {
  (void)registry; // FunctionRegistry not implemented
  return evaluateAsBool(expression, context);
}

Value ExpressionEvaluator::evaluate(const std::string &expression,
                                    const WorkflowContext &context) const {
  return evaluateAsBool(expression, context);
}

Value ExpressionEvaluator::evaluate(const std::string &expression, const WorkflowContext &context,
                                    const FunctionRegistry &registry) const {
  return evaluateAsBool(expression, context, registry);
}

const FunctionRegistry &ExpressionEvaluator::defaultRegistry() {
  static FunctionRegistry registry;
  return registry;
}

void FunctionRegistry::registerFunction(std::string name, FunctionSignature fn) {
  functions_[std::move(name)] = std::move(fn);
}

bool FunctionRegistry::hasFunction(const std::string &name) const noexcept {
  return functions_.find(name) != functions_.end();
}

const FunctionSignature *FunctionRegistry::findFunction(const std::string &name) const noexcept {
  auto it = functions_.find(name);
  return it != functions_.end() ? &it->second : nullptr;
}

} // namespace Praktor::Expressions
