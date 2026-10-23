#include "expressions/expression_evaluator.hpp"
#include "expressions/expression_evaluator.h"
#include "dag/workflow_context.hpp"
#include "util/logging.hpp"
#include <fmt.h>
#include <new>
#include <regex>

// Temporary Host/value adapter. Parsing, coercion, truthiness, comparison and
// string methods belong to C. std::regex remains an explicit provider until
// an independent C provider proves compatibility with its matching language.
namespace Praktor::Expressions {
namespace {
int host_error(char *error, size_t capacity, const char *message, int status = SALTS_EIO) {
    if (error && capacity) (void)fmt(error, capacity, "{}", message);
    return status;
}
int resolve(void *user, vstr path, size_t max_bytes, praktor_expression_value *out,
    char *error, size_t capacity) {
    try {
        const auto& context = *static_cast<const WorkflowContext *>(user);
        const auto value = context.getValueByPath(std::string(path.data, path.len));
        if (value.is_null()) return SALTS_OK;
        if (value.is_bool()) { out->kind = PRAKTOR_EXPRESSION_BOOLEAN; out->as.boolean = value.as<bool>(); }
        else if (value.is_number()) { out->kind = PRAKTOR_EXPRESSION_NUMBER; out->as.number = value.as<double>(); }
        else {
            const std::string text = value.is_string() ? value.as<std::string>() : value.to_string();
            const int status = praktor_expression_value_copy_string(vstr_from_buf(text.data(), text.size()), max_bytes, out);
            if (status != SALTS_OK) return host_error(error, capacity, "Cannot copy expression variable within its string limit", status);
        }
        return SALTS_OK;
    } catch (const std::bad_alloc& e) { return host_error(error, capacity, e.what(), SALTS_ENOMEM); }
    catch (const std::exception& e) { return host_error(error, capacity, e.what()); }
    catch (...) { return host_error(error, capacity, "Unknown expression variable Host error"); }
}
int compile(void *, vstr pattern, void **out, char *error, size_t capacity) {
    try {
        *out = new std::regex(std::string(pattern.data, pattern.len));
        return SALTS_OK;
    } catch (const std::bad_alloc& e) { return host_error(error, capacity, e.what(), SALTS_ENOMEM); }
    catch (const std::exception& e) { return host_error(error, capacity, e.what()); }
    catch (...) { return host_error(error, capacity, "Unknown expression regex compile error"); }
}
int search(void *, void *regex, vstr text, int *matches, char *error, size_t capacity) {
    try {
        const std::string input(text.data, text.len);
        *matches = std::regex_search(input, *static_cast<std::regex *>(regex));
        return SALTS_OK;
    } catch (const std::bad_alloc& e) { return host_error(error, capacity, e.what(), SALTS_ENOMEM); }
    catch (const std::exception& e) { return host_error(error, capacity, e.what()); }
    catch (...) { return host_error(error, capacity, "Unknown expression regex search error"); }
}
void destroy(void *, void *regex) { delete static_cast<std::regex *>(regex); }
struct ValueOwner {
    praktor_expression_value value{};
    ~ValueOwner() { praktor_expression_value_destroy(&value); }
};
int evaluate(const std::string& expression, const WorkflowContext& context,
    praktor_expression_value *out, int *truthy, char *error, size_t capacity) {
    const praktor_expression_host host{const_cast<WorkflowContext *>(&context), resolve, compile, search, destroy};
    return praktor_expression_evaluate(vstr_from_buf(expression.data(), expression.size()), &host,
        nullptr, out, truthy, error, capacity);
}
} // namespace

bool ExpressionEvaluator::evaluateAsBool(const std::string& expression, const WorkflowContext& context) const {
    ValueOwner value;
    int truthy = 0;
    char error[1024]{};
    if (Praktor::Expressions::evaluate(expression, context, &value.value, &truthy, error, sizeof(error)) != SALTS_OK) {
        TLOG_WARNF("Expression evaluation failed for '{}': {}", expression, error);
        return false;
    }
    return truthy != 0;
}
bool ExpressionEvaluator::evaluateAsBool(const std::string& expression, const WorkflowContext& context,
    const FunctionRegistry& registry) const {
    (void)registry;
    return evaluateAsBool(expression, context);
}
Value ExpressionEvaluator::evaluate(const std::string& expression, const WorkflowContext& context) const {
    try {
        ValueOwner value;
        char error[1024]{};
        if (Praktor::Expressions::evaluate(expression, context, &value.value, nullptr, error, sizeof(error)) != SALTS_OK) {
            TLOG_WARNF("Expression evaluation failed for '{}': {}", expression, error);
            return std::monostate{};
        }
        switch (value.value.kind) {
        case PRAKTOR_EXPRESSION_BOOLEAN: return value.value.as.boolean != 0;
        case PRAKTOR_EXPRESSION_NUMBER: return value.value.as.number;
        case PRAKTOR_EXPRESSION_STRING: return std::string(value.value.as.string, tstr_len(value.value.as.string));
        case PRAKTOR_EXPRESSION_NULL: return std::monostate{};
        }
    } catch (const std::exception& e) {
        TLOG_WARNF("Expression evaluation failed for '{}': {}", expression, e.what());
    }
    return std::monostate{};
}
Value ExpressionEvaluator::evaluate(const std::string& expression, const WorkflowContext& context,
    const FunctionRegistry& registry) const {
    (void)registry;
    return evaluate(expression, context);
}
const FunctionRegistry& ExpressionEvaluator::defaultRegistry() {
    static FunctionRegistry registry;
    return registry;
}
void FunctionRegistry::registerFunction(std::string name, FunctionSignature fn) { functions_[std::move(name)] = std::move(fn); }
bool FunctionRegistry::hasFunction(const std::string& name) const noexcept { return functions_.find(name) != functions_.end(); }
const FunctionSignature* FunctionRegistry::findFunction(const std::string& name) const noexcept {
    const auto it = functions_.find(name);
    return it != functions_.end() ? &it->second : nullptr;
}
} // namespace Praktor::Expressions
