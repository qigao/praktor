#include <uv.h> // Required for uv_os_get_passwd_alloc and uv_free_passwd
#include "util/system_info.hpp"
#include "util/logging.hpp"
#include <cstring>
#include <sstream>
#include <thread>
#include <mutex>

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

SystemInfoProvider::SystemInfoProvider() : loop_(nullptr), owns_loop_(false) {
    initializeLoop();
}

SystemInfoProvider::~SystemInfoProvider() {
    cleanupLoop();
}

void SystemInfoProvider::initializeLoop() {
    // Try to use existing loop if available, otherwise create our own
    loop_ = uv_default_loop();
    if (!loop_) {
        loop_ = new uv_loop_t;
        int result = uv_loop_init(loop_);
        if (result != 0) {
            delete loop_;
            loop_ = nullptr;
            TLOG_ERROR("Failed to initialize libuv loop: {}", uvErrorToString(result));
            return;
        }
        owns_loop_ = true;
    }
}

void SystemInfoProvider::cleanupLoop() {
    if (loop_ && owns_loop_) {
        uv_loop_close(loop_);
        delete loop_;
        loop_ = nullptr;
    }
}

std::string SystemInfoProvider::uvErrorToString(int error) const {
    return std::string(uv_strerror(error)) + " (" + uv_err_name(error) + ")";
}

std::string SystemInfoProvider::getOSName() const {
    uv_utsname_t utsname;
    int result = uv_os_uname(&utsname);
    if (result != 0) {
        TLOG_WARN("Failed to get OS name via libuv: {}", uvErrorToString(result));
        return "unknown";
    }

    std::string sysname(utsname.sysname);

    // Normalize OS names to match existing behavior
    if (sysname == "Windows_NT") return "windows";
    if (sysname == "Darwin") return "macos";
    if (sysname == "Linux") return "linux";

    // Convert to lowercase for consistency
    std::transform(sysname.begin(), sysname.end(), sysname.begin(), ::tolower);
    return sysname;
}

std::string SystemInfoProvider::getOSVersion() const {
    uv_utsname_t utsname;
    int result = uv_os_uname(&utsname);
    if (result != 0) {
        TLOG_WARN("Failed to get OS version via libuv: {}", uvErrorToString(result));
        return "unknown";
    }
    return std::string(utsname.release);
}

std::string SystemInfoProvider::getArchitecture() const {
    uv_utsname_t utsname;
    int result = uv_os_uname(&utsname);
    if (result != 0) {
        TLOG_WARN("Failed to get architecture via libuv: {}", uvErrorToString(result));
        return "unknown";
    }
    return std::string(utsname.machine);
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

     uv_passwd_t pwd;
    int result = uv_os_get_passwd(&pwd); // Pass the address of the pointer
    if (result != 0) {
        TLOG_WARN("Failed to get username via libuv: {}", uvErrorToString(result));
        return "unknown";
    }
    std::string username = pwd.username;
    uv_os_free_passwd(&pwd);
    return username;
}

std::string SystemInfoProvider::getHostname() const {
    char hostname[256];
    size_t size = sizeof(hostname);
    int result = uv_os_gethostname(hostname, &size);
    if (result != 0) {
        TLOG_WARN("Failed to get hostname via libuv: {}", uvErrorToString(result));
        return "unknown";
    }
    return std::string(hostname);
}

SystemInfoProvider::CPUInfo SystemInfoProvider::getCPUInfo() const {
    uv_cpu_info_t* cpu_infos;
    int count;
    int result = uv_cpu_info(&cpu_infos, &count);

    CPUInfo info{"unknown", 0, 0.0};

    if (result != 0) {
        TLOG_WARN("Failed to get CPU info via libuv: {}", uvErrorToString(result));
        return info;
    }

    if (count > 0) {
        info.model = std::string(cpu_infos[0].model);
        info.core_count = count;
        info.speed_mhz = cpu_infos[0].speed;
    }

    uv_free_cpu_info(cpu_infos, count);
    return info;
}

SystemInfoProvider::MemoryInfo SystemInfoProvider::getMemoryInfo() const {
    MemoryInfo info{0, 0, 0};

    info.total_memory = uv_get_total_memory();
    info.free_memory = uv_get_free_memory();
    info.available_memory = info.free_memory; // libuv doesn't distinguish available vs free

    return info;
}

std::vector<SystemInfoProvider::NetworkInterface> SystemInfoProvider::getNetworkInterfaces() const {
    std::vector<NetworkInterface> interfaces;

    uv_interface_address_t* addresses;
    int count;
    int result = uv_interface_addresses(&addresses, &count);

    if (result != 0) {
        TLOG_WARN("Failed to get network interfaces via libuv: {}", uvErrorToString(result));
        return interfaces;
    }

    for (int i = 0; i < count; i++) {
        NetworkInterface iface;
        iface.name = std::string(addresses[i].name);
        iface.is_internal = addresses[i].is_internal != 0;

        // Convert address to string
        char addr_str[INET6_ADDRSTRLEN];
        if (addresses[i].address.address4.sin_family == AF_INET) {
            uv_ip4_name(&addresses[i].address.address4, addr_str, sizeof(addr_str));
            iface.address = std::string(addr_str);

            uv_ip4_name(&addresses[i].netmask.netmask4, addr_str, sizeof(addr_str));
            iface.netmask = std::string(addr_str);
        } else if (addresses[i].address.address6.sin6_family == AF_INET6) {
            uv_ip6_name(&addresses[i].address.address6, addr_str, sizeof(addr_str));
            iface.address = std::string(addr_str);

            uv_ip6_name(&addresses[i].netmask.netmask6, addr_str, sizeof(addr_str));
            iface.netmask = std::string(addr_str);
        }

        interfaces.push_back(std::move(iface));
    }

    uv_free_interface_addresses(addresses, count);
    return interfaces;
}

SystemInfoProvider::LoadAverage SystemInfoProvider::getLoadAverage() const {
    LoadAverage load{0.0, 0.0, 0.0};

    double avg[3];
    uv_loadavg(avg);

    load.one_minute = avg[0];
    load.five_minutes = avg[1];
    load.fifteen_minutes = avg[2];

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
    uv_env_item_t* env_items;
    int count;
    int result = uv_os_environ(&env_items, &count);

    if (result != 0) {
        TLOG_WARN("Failed to get environment variables via libuv: {}", uvErrorToString(result));
        return env_vars;
    }

    for (int i = 0; i < count; ++i) {
        env_vars[env_items[i].name] = env_items[i].value;
    }

    uv_os_free_environ(env_items, count);
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
