#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <future>
#include <uv.h>

namespace Praktor {
namespace system {

/**
 * @brief libuv-based system information provider
 *
 * This implementation uses libuv's cross-platform system APIs for consistent
 * and efficient system information retrieval. It's non-blocking and integrates
 * well with the existing libuv event loop used for process execution.
 */
class SystemInfoProvider {
public:
    SystemInfoProvider();
    ~SystemInfoProvider();

    // Non-copyable, movable
    SystemInfoProvider(const SystemInfoProvider&) = delete;
    SystemInfoProvider& operator=(const SystemInfoProvider&) = delete;
    SystemInfoProvider(SystemInfoProvider&&) = default;
    SystemInfoProvider& operator=(SystemInfoProvider&&) = default;

    /**
     * @brief Gets the operating system name using libuv
     * @return The OS name (e.g., "windows", "linux", "darwin")
     */
    std::string getOSName() const;

    /**
     * @brief Gets the operating system version using libuv
     * @return The OS version string
     */
    std::string getOSVersion() const;

    /**
     * @brief Gets the system architecture using libuv
     * @return The architecture string (e.g., "x86_64", "arm64")
     */
    std::string getArchitecture() const;

    /**
     * @brief Gets the current shell name
     * @return The shell name (e.g., "cmd", "powershell", "bash", "zsh")
     */
    std::string getShellName() const;

    /**
     * @brief Gets the current username using libuv
     * @return The current username
     */
    std::string getUsername() const;

    /**
     * @brief Gets the hostname using libuv
     * @return The system hostname
     */
    std::string getHostname() const;

    /**
     * @brief Gets CPU information using libuv
     * @return CPU model name and core count
     */
    struct CPUInfo {
        std::string model;
        int core_count;
        double speed_mhz;
    };
    CPUInfo getCPUInfo() const;

    /**
     * @brief Gets memory information using libuv
     * @return Memory information in bytes
     */
    struct MemoryInfo {
        uint64_t total_memory;
        uint64_t free_memory;
        uint64_t available_memory;
    };
    MemoryInfo getMemoryInfo() const;

    /**
     * @brief Gets network interface information using libuv
     * @return List of network interfaces
     */
    struct NetworkInterface {
        std::string name;
        std::string address;
        std::string netmask;
        bool is_internal;
    };
    std::vector<NetworkInterface> getNetworkInterfaces() const;

    /**
     * @brief Gets load average (Unix-like systems only)
     * @return Load averages for 1, 5, and 15 minutes
     */
    struct LoadAverage {
        double one_minute;
        double five_minutes;
        double fifteen_minutes;
    };
    LoadAverage getLoadAverage() const;

    /**
     * @brief Gets all built-in system properties using libuv APIs
     * @return A map of system property names to values
     */
    std::unordered_map<std::string, std::string> getAllSystemProperties() const;

    /**
     * @brief Gets all environment variables using libuv
     * @return A map of environment variable names to values
     */
    std::unordered_map<std::string, std::string> getEnvironmentVariables() const;

    /**
     * @brief Gets extended system properties including hardware info
     * @return A map with additional hardware and performance metrics
     */
    std::unordered_map<std::string, std::string> getExtendedSystemProperties() const;

    /**
     * @brief Async version of getAllSystemProperties for non-blocking operation
     * @return Future containing system properties
     */
    std::future<std::unordered_map<std::string, std::string>> getAllSystemPropertiesAsync() const;

private:
    mutable uv_loop_t* loop_;
    bool owns_loop_;

    // Helper methods
    std::string uvErrorToString(int error) const;
    void initializeLoop();
    void cleanupLoop();
};

// Convenience functions that use a singleton instance
std::string getOSName();
std::string getOSVersion();
std::string getArchitecture();
std::string getShellName();
std::string getUsername();
std::string getHostname();
std::unordered_map<std::string, std::string> getAllSystemProperties();
std::unordered_map<std::string, std::string> getEnvironmentVariables();

} // namespace system
} // namespace Praktor
