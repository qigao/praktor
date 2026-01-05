#ifndef __EXPRESSION_EVALUATOR_HPP__
#define __EXPRESSION_EVALUATOR_HPP__

#include "dag/workflow_context.hpp"
#include <string>
#include <memory>
#include <variant>

namespace Praktor::Util {

    /**
     * @class ExpressionEvaluator
     * @brief Parses and evaluates a 'when' condition string using a PEGTL grammar.
     */
    class ExpressionEvaluator {
    public:
        /**
         * @brief Evaluates the given expression string within a context.
         * @param expression The expression to evaluate (e.g., "{{env}} == 'prod'").
         * @param context The workflow context for variable substitution.
         * @return The boolean result of the expression.
         */
        bool evaluate(const std::string& expression, const WorkflowContext& context);

    private:
        // Removed simple evaluator - unified under PEGTL
    };

} // namespace Praktor::Util

#endif // __EXPRESSION_EVALUATOR_HPP__
