#ifndef __SCOPED_VARIABLES_HPP__
#define __SCOPED_VARIABLES_HPP__

#include "workflow_context.hpp"
#include "yml/task_types.hpp"
#include "util/variable_substitution.hpp"

#include <string>
#include <unordered_map>
#include <vector>

/**
 * @class ScopedVariables
 * @brief RAII wrapper for managing temporary variable scopes in WorkflowContext
 *
 * This class automatically applies variables when constructed and restores
 * the previous state when destructed, eliminating manual save/restore logic.
 *
 * Linus principle: "Use data structures and RAII to eliminate special cases"
 * Before: 100+ lines of manual applyTaskVariables/restoreTaskVariables
 * After: Constructor/destructor does it automatically
 */
class ScopedVariables {
public:
    /**
     * @brief Constructs and applies variables to context
     * @param context The workflow context to modify
     * @param variables The variables to apply (will be evaluated with substitution)
     */
    ScopedVariables(WorkflowContext& context, const Vars& variables)
        : context_(context)
    {
        for (const auto& [key, value] : variables) {
            if (context_.hasKey(key)) {
                try {
                    original_values_[key] = context_.getValue<WorkflowValue>(key);
                } catch (const std::exception&) {
                    original_values_[key] = WorkflowValue();
                }
            } else {
                new_keys_.push_back(key);
            }
            
            std::string evaluated = substituteVariables(value, context_);
            context_.setValue(key, evaluated);
        }
    }

    // Prevent copying (RAII should not be copied)
    ScopedVariables(const ScopedVariables&) = delete;
    ScopedVariables& operator=(const ScopedVariables&) = delete;

    // Allow moving if needed
    ScopedVariables(ScopedVariables&&) noexcept = default;
    ScopedVariables& operator=(ScopedVariables&&) noexcept = default;

    /**
     * @brief Destructor automatically restores previous variable state
     */
    ~ScopedVariables() {
        restore();
    }

    /**
     * @brief Manually restore variables (called automatically by destructor)
     * 
     * This is public to allow early restoration if needed, though
     * the destructor will call it automatically.
     */
    void restore() {
        if (restored_) return;
        
        for (const auto& key : new_keys_) {
            context_.unsetValue(key);
        }
        
        for (const auto& [key, value] : original_values_) {
            context_.setValue(key, value);
        }
        
        restored_ = true;
    }

private:
    WorkflowContext& context_;
    std::vector<std::string> new_keys_;
    std::unordered_map<std::string, WorkflowValue> original_values_;
    bool restored_ = false;
};

#endif // __SCOPED_VARIABLES_HPP__
