#include "util/system_info.hpp"
#include "util/logging.hpp"
#include "platform.h"
#include <cstring>
#include <sstream>
#include <thread>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#else
extern char **environ;
#endif

namespace Praktor {
namespace system {

namespace {
    // Singleton instance for convenience functions
    std::once_flag init_flag;
    std::unique_ptr<SystemInfoProvider> g_provider;

    void initializeProvider() {
        g_provider = std::make_unique<SystemInfoProvider>();
    }

    SystemInfoProvider& getProvider() {
        std::call_once(init_flag, initializeProvider);
        return *g_provider;
    }
}

std::string SystemInfoProvider::getOSName() const {
    char value[TURBO_PLATFORM_INFO_MAX] = {0};
    int result = turbo_platform_os_name(value, sizeof(value));
    if (result != 0) {
        TLOG_WARN("Failed to get OS name via platform API: {}", result);
        return "unknown";
    }
    return std::string(value);
}

std::string SystemInfoProvider::getOSVersion() const {
    char value[TURBO_PLATFORM_INFO_MAX] = {0};
    int result = turbo_platform_os_version(value, sizeof(value));
    if (result != 0) {
        TLOG_WARN("Failed to get OS version via platform API: {}", result);
        return "unknown";
    }
    return std::string(value);
}

std::string SystemInfoProvider::getArchitecture() const {
    char value[TURBO_PLATFORM_INFO_MAX] = {0};
    int result = turbo_platform_arch(value, sizeof(value));
    if (result != 0) {
        TLOG_WARN("Failed to get architecture via platform API: {}", result);
        return "unknown";
    }
    return std::string(value);
}

std::string SystemInfoProvider::getShellName() const {
    // Shell detection is environment-based, not a libuv system call
    const char* shell = std::getenv("SHELL");
    if (shell) {
        std::string shellPath(shell);
        size_t lastSlash = shellPath.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            return shellPath.substr(lastSlash + 1);
        }
        return shellPath;
    }

#ifdef _WIN32
    // Check for PowerShell on Windows
    const char* psModulePath = std::getenv("PSModulePath");
    if (psModulePath && strlen(psModulePath) > 0) {
        return "powershell";
    }
    return "cmd";
#else
    return "unknown";
#endif
}

std::string SystemInfoProvider::getUsername() const {
    char value[TURBO_PLATFORM_INFO_MAX] = {0};
    int result = turbo_platform_username(value, sizeof(value));
    if (result != 0) {
        TLOG_WARN("Failed to get username via platform API: {}", result);
        return "unknown";
    }
    return std::string(value);
}

std::string SystemInfoProvider::getHostname() const {
    char value[TURBO_PLATFORM_INFO_MAX] = {0};
    int result = turbo_platform_hostname(value, sizeof(value));
    if (result != 0) {
        TLOG_WARN("Failed to get hostname via platform API: {}", result);
        return "unknown";
    }
    return std::string(value);
}

SystemInfoProvider::CPUInfo SystemInfoProvider::getCPUInfo() const {
    CPUInfo info{"unknown", 0, 0.0};
    turbo_platform_cpu_info_t native_info{};
    int result = turbo_platform_cpu_info(&native_info);
    if (result != 0) {
        TLOG_WARN("Failed to get CPU info via platform API: {}", result);
        return info;
    }
    info.model = native_info.model;
    info.core_count = native_info.core_count;
    info.speed_mhz = native_info.speed_mhz;
    return info;
}

SystemInfoProvider::MemoryInfo SystemInfoProvider::getMemoryInfo() const {
    MemoryInfo info{0, 0, 0};
    turbo_platform_memory_info_t native_info{};
    int result = turbo_platform_memory_info(&native_info);
    if (result != 0) {
        TLOG_WARN("Failed to get memory info via platform API: {}", result);
        return info;
    }
    info.total_memory = native_info.total_memory;
    info.free_memory = native_info.free_memory;
    info.available_memory = native_info.available_memory;
    return info;
}

std::vector<SystemInfoProvider::NetworkInterface> SystemInfoProvider::getNetworkInterfaces() const {
    std::vector<NetworkInterface> interfaces;
    turbo_platform_network_interface_t native_interfaces[64] = {};
    size_t count = 0;
    int result = turbo_platform_network_interfaces(native_interfaces, 64, &count);
    if (result != 0) {
        TLOG_WARN("Failed to get network interfaces via platform API: {}", result);
        return interfaces;
    }
    interfaces.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        NetworkInterface iface;
        iface.name = native_interfaces[i].name;
        iface.address = native_interfaces[i].address;
        iface.netmask = native_interfaces[i].netmask;
        iface.is_internal = native_interfaces[i].is_internal != 0;
        interfaces.push_back(std::move(iface));
    }
    return interfaces;
}

SystemInfoProvider::LoadAverage SystemInfoProvider::getLoadAverage() const {
    LoadAverage load{0.0, 0.0, 0.0};
    turbo_platform_load_average_t native_info{};
    int result = turbo_platform_load_average(&native_info);
    if (result != 0) {
        TLOG_WARN("Failed to get load average via platform API: {}", result);
        return load;
    }
    load.one_minute = native_info.one_minute;
    load.five_minutes = native_info.five_minutes;
    load.fifteen_minutes = native_info.fifteen_minutes;
    return load;
}

std::unordered_map<std::string, std::string> SystemInfoProvider::getAllSystemProperties() const {
    std::unordered_map<std::string, std::string> props;

    // Basic system info
    props["PRAKTOR_OS_NAME"] = getOSName();
    props["PRAKTOR_OS_VERSION"] = getOSVersion();
    props["PRAKTOR_ARCH"] = getArchitecture();
    props["PRAKTOR_SHELL"] = getShellName();
    props["PRAKTOR_USER"] = getUsername();
    props["PRAKTOR_HOST"] = getHostname();

    return props;
}

std::unordered_map<std::string, std::string> SystemInfoProvider::getEnvironmentVariables() const {
    std::unordered_map<std::string, std::string> env_vars;

#ifdef _WIN32
    LPCH block = GetEnvironmentStringsA();
    if (!block) {
        TLOG_WARN("Failed to get environment variables via Win32 API");
        return env_vars;
    }

    for (LPCH current = block; *current != '\0'; current += std::strlen(current) + 1) {
        std::string entry(current);
        size_t separator = entry.find('=');
        if (separator == std::string::npos || separator == 0) {
            continue;
        }
        env_vars[entry.substr(0, separator)] = entry.substr(separator + 1);
    }

    FreeEnvironmentStringsA(block);
#else
    for (char **current = environ; current && *current; ++current) {
        std::string entry(*current);
        size_t separator = entry.find('=');
        if (separator == std::string::npos || separator == 0) {
            continue;
        }
        env_vars[entry.substr(0, separator)] = entry.substr(separator + 1);
    }
#endif

    return env_vars;
}

std::unordered_map<std::string, std::string> SystemInfoProvider::getExtendedSystemProperties() const {
    auto props = getAllSystemProperties();

    // Add environment variables
    auto env_vars = getEnvironmentVariables();
    for (const auto& pair : env_vars) {
        props["PRAKTOR_ENV_" + pair.first] = pair.second;
    }

    // CPU information
    auto cpu_info = getCPUInfo();
    props["PRAKTOR_CPU_MODEL"] = cpu_info.model;
    props["PRAKTOR_CPU_CORES"] = std::to_string(cpu_info.core_count);
    props["PRAKTOR_CPU_SPEED_MHZ"] = std::to_string(static_cast<int>(cpu_info.speed_mhz));

    // Memory information
    auto mem_info = getMemoryInfo();
    props["PRAKTOR_MEMORY_TOTAL_GB"] = std::to_string(mem_info.total_memory / (1024 * 1024 * 1024));
    props["PRAKTOR_MEMORY_FREE_GB"] = std::to_string(mem_info.free_memory / (1024 * 1024 * 1024));

    // Load average (Unix-like systems)
    auto load = getLoadAverage();
    props["PRAKTOR_LOAD_1MIN"] = std::to_string(load.one_minute);
    props["PRAKTOR_LOAD_5MIN"] = std::to_string(load.five_minutes);
    props["PRAKTOR_LOAD_15MIN"] = std::to_string(load.fifteen_minutes);

    // Network interfaces count
    auto interfaces = getNetworkInterfaces();
    props["PRAKTOR_NETWORK_INTERFACES"] = std::to_string(interfaces.size());

    return props;
}

std::future<std::unordered_map<std::string, std::string>> SystemInfoProvider::getAllSystemPropertiesAsync() const {
    return std::async(std::launch::async, [this]() {
        return getAllSystemProperties();
    });
}

// Convenience functions using singleton
std::string getOSName() {
    return getProvider().getOSName();
}

std::string getOSVersion() {
    return getProvider().getOSVersion();
}

std::string getArchitecture() {
    return getProvider().getArchitecture();
}

std::string getShellName() {
    return getProvider().getShellName();
}

std::string getUsername() {
    return getProvider().getUsername();
}

std::string getHostname() {
    return getProvider().getHostname();
}

std::unordered_map<std::string, std::string> getAllSystemProperties() {
    return getProvider().getAllSystemProperties();
}

std::unordered_map<std::string, std::string> getEnvironmentVariables() {
    return getProvider().getEnvironmentVariables();
}

} // namespace system
} // namespace Praktor
