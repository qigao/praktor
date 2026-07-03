#pragma once

#include "yml/task_types.hpp"

#include <jsoncons/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace Praktor::Native {

// Function signature for DSL call dispatch (pure C ABI)
// Returns bytes written to out_buf, -1 on error, > out_buf_size if buffer too small
using NativeCallFunc = int (*)(const char* func_name, const char* args_json, char* out_buf, size_t out_buf_size);

/**
 * @class NativeLoader
 * @brief Loads and manages native DLL/SO modules via pure C ABI
 *
 * Single-stage loading: dlopen + resolve hook symbols from YAML declaration.
 * All interaction goes through callFunction() dispatching to hooks["call"].
 */
class NativeLoader {
public:
    ~NativeLoader();

    NativeLoader(const NativeLoader&) = delete;
    NativeLoader& operator=(const NativeLoader&) = delete;

    static NativeLoader& instance();

    // Load a single module library (dlopen + resolve hooks)
    bool loadModuleLibrary(const NativeModule& module, const std::string& base_path);

    // Batch load all module libraries
    void loadModuleLibraries(const NativeModules& modules, const std::string& base_path);

    // DSL call dispatch via hooks["call"]
    jsoncons::json callFunction(const std::string& module_name, const std::string& func_name,
                                const std::vector<std::string>& args);

    void unloadAll();

private:
    NativeLoader() = default;

    struct LoadedModule {
        void* handle = nullptr;
        std::string path;
        std::unordered_map<std::string, void*> resolved_hooks;
    };

    std::unordered_map<std::string, LoadedModule> loaded_modules_;

    void* loadLibrary(const std::string& path);
    void* getSymbol(void* handle, const std::string& name);
    void freeLibrary(void* handle);
    std::string getLastError();
};

} // namespace Praktor::Native
