#ifndef __VARIABLE_SCOPE_HPP__
#define __VARIABLE_SCOPE_HPP__

#include <jsoncons/json.hpp>
#include <memory>
#include <string>
#include <unordered_map>

/**
 * @class VariableScope
 * @brief Manages variable scoping with parent chain support
 *
 * Linus principle: "Use the right data structure to eliminate special cases"
 * 
 * This class implements a scope chain where child scopes can access parent variables
 * but modifications only affect the current scope. This is how proper lexical scoping works.
 *
 * Before: Global flat dictionary with manual save/restore
 * After: Tree structure with automatic inheritance
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
    void set(const std::string& key, const jsoncons::json& value) {
        local_[key] = value;
    }

    /**
     * @brief Get a variable, searching up the scope chain
     * @throws std::runtime_error if key not found
     */
    jsoncons::json get(const std::string& key) const {
        auto it = local_.find(key);
        if (it != local_.end()) {
            return it->second;
        }
        
        if (parent_) {
            return parent_->get(key);
        }
        
        throw std::runtime_error("Variable not found: " + key);
    }

    /**
     * @brief Get a variable with default value
     */
    template<typename T>
    T getOr(const std::string& key, const T& default_value) const {
        try {
            return get(key).as<T>();
        } catch (...) {
            return default_value;
        }
    }

    /**
     * @brief Check if variable exists in scope chain
     */
    bool has(const std::string& key) const {
        if (local_.count(key)) {
            return true;
        }
        return parent_ ? parent_->has(key) : false;
    }

    /**
     * @brief Remove a variable from current scope only
     */
    void remove(const std::string& key) {
        local_.erase(key);
    }

    /**
     * @brief Get all variables visible in current scope (includes parent chain)
     */
    std::unordered_map<std::string, jsoncons::json> getAllVisible() const {
        std::unordered_map<std::string, jsoncons::json> result;
        
        // Start with parent's variables
        if (parent_) {
            result = parent_->getAllVisible();
        }
        
        // Override with local variables
        for (const auto& [key, value] : local_) {
            result[key] = value;
        }
        
        return result;
    }

    /**
     * @brief Get only local variables (not including parent)
     */
    const std::unordered_map<std::string, jsoncons::json>& getLocal() const {
        return local_;
    }

    /**
     * @brief Get parent scope
     */
    VariableScope* getParent() const {
        return parent_;
    }

private:
    std::unordered_map<std::string, jsoncons::json> local_;
    VariableScope* parent_;
};

#endif // __VARIABLE_SCOPE_HPP__
