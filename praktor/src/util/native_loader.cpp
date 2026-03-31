#include "util/native_loader.hpp"
#include "util/logging.hpp"

#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string getExecutablePath() {
#ifdef _WIN32
  char buf[MAX_PATH];
  DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (len > 0 && len < MAX_PATH) return std::string(buf, len);
  return {};
#else
  char buf[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len > 0) { buf[len] = '\0'; return std::string(buf); }
  return {};
#endif
}

} // namespace

namespace Praktor::Native {

NativeLoader &NativeLoader::instance() {
  static NativeLoader instance;
  return instance;
}

NativeLoader::~NativeLoader() { unloadAll(); }

bool NativeLoader::loadModuleLibrary(const NativeModule &module, const std::string &base_path) {
  if (loaded_modules_.count(module.name)) {
    logd("Module '{}' already loaded, skipping", module.name);
    return true;
  }

  fs::path module_path = module.path;
  std::string abs_path;

  if (module_path.is_absolute()) {
    abs_path = module_path.lexically_normal().string();
  } else {
    // Resolve relative to executable directory first
    std::error_code ec;
    auto exe_dir = fs::path(getExecutablePath()).parent_path();
    auto exe_candidate = (exe_dir / module_path).lexically_normal();
    if (fs::exists(exe_candidate, ec)) {
      abs_path = fs::absolute(exe_candidate).lexically_normal().string();
    } else {
      // Fall back to YAML source directory
      auto yaml_dir = fs::path(base_path).parent_path();
      abs_path = fs::absolute(yaml_dir / module_path).lexically_normal().string();
    }
  }

  if (!fs::exists(abs_path)) {
    loge("Native module file not found: {}", abs_path);
    return false;
  }

  void *handle = loadLibrary(abs_path);
  if (!handle) {
    loge("Failed to load native module '{}': {}", module.name, getLastError());
    return false;
  }

  LoadedModule loaded;
  loaded.handle = handle;
  loaded.path = abs_path;

  for (const auto &[hook_name, symbol_name] : module.hooks) {
    void *sym = getSymbol(handle, symbol_name);
    if (!sym) {
      logw("Hook '{}' symbol '{}' not found in module '{}': {}", hook_name, symbol_name,
           module.name, getLastError());
      continue;
    }
    loaded.resolved_hooks[hook_name] = sym;
    logd("Resolved hook '{}' -> '{}' in module '{}'", hook_name, symbol_name, module.name);
  }

  loaded_modules_[module.name] = std::move(loaded);
  logd("Loaded native module '{}' from {}", module.name, abs_path);
  return true;
}

void NativeLoader::loadModuleLibraries(const NativeModules &modules, const std::string &base_path) {
  for (const auto &module : modules) {
    if (!loadModuleLibrary(module, base_path)) {
      throw std::runtime_error("Failed to load native module library: " + module.name);
    }
  }
}

jsoncons::json NativeLoader::callFunction(const std::string &module_name,
                                          const std::string &func_name,
                                          const std::vector<std::string> &args) {
  auto it = loaded_modules_.find(module_name);
  if (it == loaded_modules_.end()) {
    throw std::runtime_error("Module '" + module_name + "' not loaded");
  }

  auto hook_it = it->second.resolved_hooks.find("call");
  if (hook_it == it->second.resolved_hooks.end()) {
    throw std::runtime_error("Module '" + module_name + "' has no 'call' hook");
  }

  auto call_func = reinterpret_cast<NativeCallFunc>(hook_it->second);

  jsoncons::json args_json = jsoncons::json::array();
  for (const auto &arg : args) {
    args_json.push_back(arg);
  }
  std::string args_str = args_json.to_string();

  constexpr size_t initial_buf_size = 4096;
  std::vector<char> buf(initial_buf_size);

  int written = call_func(func_name.c_str(), args_str.c_str(), buf.data(), buf.size());

  if (written < 0) {
    throw std::runtime_error("Call to " + module_name + "." + func_name + " failed");
  }

  if (static_cast<size_t>(written) > buf.size()) {
    buf.resize(static_cast<size_t>(written));
    written = call_func(func_name.c_str(), args_str.c_str(), buf.data(), buf.size());
    if (written < 0) {
      throw std::runtime_error("Call to " + module_name + "." + func_name + " failed on retry");
    }
  }

  if (written == 0) {
    return jsoncons::json::null();
  }

  try {
    return jsoncons::json::parse(std::string_view(buf.data(), static_cast<size_t>(written)));
  } catch (const std::exception&) {
    return jsoncons::json(std::string(buf.data(), static_cast<size_t>(written)));
  }
}

void NativeLoader::unloadAll() {
  for (auto &[name, module] : loaded_modules_) {
    if (module.handle) {
      freeLibrary(module.handle);
      module.handle = nullptr;
    }
  }
  loaded_modules_.clear();
}

// Platform-specific implementations

#ifdef _WIN32

void *NativeLoader::loadLibrary(const std::string &path) { return LoadLibraryA(path.c_str()); }

void *NativeLoader::getSymbol(void *handle, const std::string &name) {
  return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(handle), name.c_str()));
}

void NativeLoader::freeLibrary(void *handle) { FreeLibrary(static_cast<HMODULE>(handle)); }

std::string NativeLoader::getLastError() {
  DWORD error = GetLastError();
  if (error == 0)
    return "Unknown error";

  LPSTR buffer = nullptr;
  size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               reinterpret_cast<LPSTR>(&buffer), 0, nullptr);

  std::string message(buffer, size);
  LocalFree(buffer);

  while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
    message.pop_back();
  }
  return message;
}

#else // POSIX

void *NativeLoader::loadLibrary(const std::string &path) {
  return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}

void *NativeLoader::getSymbol(void *handle, const std::string &name) {
  return dlsym(handle, name.c_str());
}

void NativeLoader::freeLibrary(void *handle) { dlclose(handle); }

std::string NativeLoader::getLastError() {
  const char *error = dlerror();
  return error ? error : "Unknown error";
}

#endif

} // namespace Praktor::Native
