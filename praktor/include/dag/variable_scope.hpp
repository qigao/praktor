#pragma once

#include "data/workflow_value.hpp"
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

/**
 * @class VariableScope
 * @brief Manages variable scoping with parent chain support
 * 
 * This class implements a scope chain where child scopes can access parent variables
 * but modifications only affect the current scope. This is how proper lexical scoping works.
 *
 * Tree structure with automatic inheritance
 *
 * Example:
 *   Root scope: { APP_NAME: "myapp", VERSION: "1.0" }
 *   Task scope: { TASK_VAR: "value" }
 *   
 *   get("TASK_VAR") -> finds in current scope
 *   get("VERSION")  -> walks up to root scope
 *   set("VERSION", "2.0") -> only modifies current scope, doesn't affect parent
 */
class VariableScope {
public:
    /**
     * @brief Create a root scope (no parent)
     */
    VariableScope() : parent_(nullptr) {}

    /**
     * @brief Create a child scope
     * @param parent Parent scope for inheritance
     */
    explicit VariableScope(VariableScope* parent) : parent_(parent) {}

    /**
     * @brief Set a variable in the current scope
     */
    void set(const std::string& key, const WorkflowValue& value) {
        std::unique_lock lock(mutex_);
        local_[key] = value;
    }

    /**
     * @brief Get a variable, searching up the scope chain
     * @throws std::runtime_error if key not found
     */
    WorkflowValue get(const std::string& key) const {
        VariableScope* parent = nullptr;
        {
            std::shared_lock lock(mutex_);
            auto it = local_.find(key);
            if (it != local_.end()) {
                return it->second;
            }
            parent = parent_;
        }

        if (parent) {
            return parent->get(key);
        }

        throw std::runtime_error("Variable not found: " + key);
    }

    /**
     * @brief Get a variable with default value
     */
    template<typename T>
    T getOr(const std::string& key, const T& default_value) const {
        if (!has(key)) return default_value;
        return get(key).as<T>();
    }

    /**
     * @brief Check if variable exists in scope chain
     */
    bool has(const std::string& key) const {
        VariableScope* parent = nullptr;
        {
            std::shared_lock lock(mutex_);
            if (local_.count(key) > 0) {
                return true;
            }
            parent = parent_;
        }
        return parent ? parent->has(key) : false;
    }

    /**
     * @brief Remove a variable from current scope only
     */
    void remove(const std::string& key) {
        std::unique_lock lock(mutex_);
        local_.erase(key);
    }

    /**
     * @brief Get all variables visible in current scope (includes parent chain)
     */
    std::unordered_map<std::string, WorkflowValue> getAllVisible() const {
        std::unordered_map<std::string, WorkflowValue> result;

        VariableScope* parent = nullptr;
        {
            std::shared_lock lock(mutex_);
            parent = parent_;
        }

        if (parent) {
            result = parent->getAllVisible();
        }

        {
            std::shared_lock lock(mutex_);
            for (const auto& [key, value] : local_) {
                result[key] = value;
            }
        }

        return result;
    }

    /**
     * @brief Get only local variables (not including parent)
     */
    std::unordered_map<std::string, WorkflowValue> getLocalSnapshot() const {
        std::shared_lock lock(mutex_);
        return local_;
    }

    /**
     * @brief Get parent scope
     */
    VariableScope* getParent() const {
        return parent_;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, WorkflowValue> local_;
    VariableScope* parent_;
};

