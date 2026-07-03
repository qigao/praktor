#pragma once

#include <string>
#include <unordered_map>

#include "yml/task_types.hpp"

/**
 * @class WorkflowModuleStore
 * @brief Holds embedded and native modules for a workflow execution context.
 */
class WorkflowModuleStore {
public:
    void setEmbeddedModules(const std::unordered_map<std::string, EmbeddedModule>& modules) {
        embedded_modules_ = modules;
    }

    bool hasEmbeddedModule(const std::string& name) const {
        return embedded_modules_.find(name) != embedded_modules_.end();
    }

    const EmbeddedModule* getEmbeddedModule(const std::string& name) const {
        auto it = embedded_modules_.find(name);
        if (it == embedded_modules_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    const std::unordered_map<std::string, EmbeddedModule>& getEmbeddedModules() const {
        return embedded_modules_;
    }

    void setNativeModules(const NativeModules& modules) {
        native_modules_ = modules;
    }

    const NativeModules& getNativeModules() const {
        return native_modules_;
    }

private:
    std::unordered_map<std::string, EmbeddedModule> embedded_modules_;
    NativeModules native_modules_;
};
