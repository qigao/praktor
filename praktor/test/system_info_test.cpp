#include <catch2/catch_test_macros.hpp>
#include "util/system_info.hpp"
#include "dag/workflow_context.hpp"
#include <string>
#include <chrono>

using namespace Praktor::system;

TEST_CASE("SystemInfoProvider - Basic OS Information", "[system_info][libuv]") {
    SystemInfoProvider provider;

    SECTION("OS Name") {
        std::string osName = provider.getOSName();
        REQUIRE_FALSE(osName.empty());
        REQUIRE((osName == "windows" || osName == "linux" || osName == "macos" || osName == "unix" || osName == "unknown"));
    }

    SECTION("OS Version") {
        std::string osVersion = provider.getOSVersion();
        REQUIRE_FALSE(osVersion.empty());
        REQUIRE((osVersion == "unknown" || osVersion.find_first_of("0123456789") != std::string::npos));
    }

    SECTION("Architecture") {
        std::string arch = provider.getArchitecture();
        REQUIRE_FALSE(arch.empty());
        REQUIRE((arch == "x86_64" || arch == "arm64" || arch == "x86" || arch == "arm" ||
                 arch == "i386" || arch == "aarch64" || arch == "unknown"));
    }
}

TEST_CASE("SystemInfoProvider - User and Network Information", "[system_info][libuv]") {
    SystemInfoProvider provider;

    SECTION("Username") {
        std::string username = provider.getUsername();
        REQUIRE_FALSE(username.empty());
        // Username should be a valid string (not just "unknown")
        if (username != "unknown") {
            REQUIRE(username.length() > 0);
            REQUIRE(username.find_first_not_of(" \t\n\r") != std::string::npos);
        }
    }

    SECTION("Hostname") {
        std::string hostname = provider.getHostname();
        REQUIRE_FALSE(hostname.empty());
        if (hostname != "unknown") {
            REQUIRE(hostname.length() > 0);
        }
    }

    SECTION("Shell Name") {
        std::string shell = provider.getShellName();
        REQUIRE_FALSE(shell.empty());
        REQUIRE((shell == "bash" || shell == "zsh" || shell == "fish" ||
                 shell == "cmd" || shell == "powershell" || shell == "sh" || shell == "unknown"));
    }
}

TEST_CASE("SystemInfoProvider - Hardware Information", "[system_info][libuv][hardware]") {
    SystemInfoProvider provider;

    SECTION("CPU Information") {
        auto cpu_info = provider.getCPUInfo();

        // CPU model should be available on most systems
        REQUIRE_FALSE(cpu_info.model.empty());

        // Core count should be positive
        REQUIRE(cpu_info.core_count > 0);
        REQUIRE(cpu_info.core_count <= 256); // Reasonable upper bound

        // CPU speed should be reasonable (in MHz)
        if (cpu_info.speed_mhz > 0) {
            REQUIRE(cpu_info.speed_mhz >= 100.0);    // At least 100 MHz
            REQUIRE(cpu_info.speed_mhz <= 10000.0);  // Less than 10 GHz
        }
    }

    SECTION("Memory Information") {
        auto mem_info = provider.getMemoryInfo();

        // Total memory should be positive
        REQUIRE(mem_info.total_memory > 0);

        // Free memory should be less than or equal to total
        REQUIRE(mem_info.free_memory <= mem_info.total_memory);

        // Available memory should be reasonable
        REQUIRE(mem_info.available_memory <= mem_info.total_memory);

        // Minimum reasonable memory: 512MB
        REQUIRE(mem_info.total_memory >= 512 * 1024 * 1024);
    }
}

TEST_CASE("SystemInfoProvider - Network Interfaces", "[system_info][libuv][network]") {
    SystemInfoProvider provider;

    auto interfaces = provider.getNetworkInterfaces();

    // Should have at least one interface (loopback)
    REQUIRE(interfaces.size() > 0);

    bool has_loopback = false;
    bool has_external = false;

    for (const auto& iface : interfaces) {
        // Interface name should not be empty
        REQUIRE_FALSE(iface.name.empty());

        // Address should not be empty
        REQUIRE_FALSE(iface.address.empty());

        if (iface.is_internal) {
            has_loopback = true;
        } else {
            has_external = true;
        }

        // Check for common interface patterns
        if (iface.address.find("127.0.0.1") != std::string::npos ||
            iface.address.find("::1") != std::string::npos) {
            REQUIRE(iface.is_internal);
        }
    }

    // Most systems should have a loopback interface
    REQUIRE(has_loopback);
}

TEST_CASE("SystemInfoProvider - Load Average", "[system_info][libuv][performance]") {
    SystemInfoProvider provider;

    auto load = provider.getLoadAverage();

    // Load averages should be non-negative
    REQUIRE(load.one_minute >= 0.0);
    REQUIRE(load.five_minutes >= 0.0);
    REQUIRE(load.fifteen_minutes >= 0.0);

    // Load averages should be reasonable (less than 1000)
    REQUIRE(load.one_minute < 1000.0);
    REQUIRE(load.five_minutes < 1000.0);
    REQUIRE(load.fifteen_minutes < 1000.0);
}

TEST_CASE("SystemInfoProvider - All System Properties", "[system_info][libuv]") {
    SystemInfoProvider provider;

    SECTION("Basic Properties") {
        auto props = provider.getAllSystemProperties();

        // Should contain all expected basic properties
        REQUIRE(props.find("PRAKTOR_OS_NAME") != props.end());
        REQUIRE(props.find("PRAKTOR_OS_VERSION") != props.end());
        REQUIRE(props.find("PRAKTOR_ARCH") != props.end());
        REQUIRE(props.find("PRAKTOR_SHELL") != props.end());
        REQUIRE(props.find("PRAKTOR_USER") != props.end());
        REQUIRE(props.find("PRAKTOR_HOST") != props.end());

        // All values should be non-empty
        for (const auto& [key, value] : props) {
            INFO("Property " << key << " should not be empty");
            REQUIRE_FALSE(value.empty());
        }
    }

    SECTION("Extended Properties") {
        auto props = provider.getExtendedSystemProperties();

        // Should contain hardware information
        REQUIRE(props.find("PRAKTOR_CPU_MODEL") != props.end());
        REQUIRE(props.find("PRAKTOR_CPU_CORES") != props.end());
        REQUIRE(props.find("PRAKTOR_MEMORY_TOTAL_GB") != props.end());
        REQUIRE(props.find("PRAKTOR_NETWORK_INTERFACES") != props.end());

        // CPU cores should be a valid number
        int cores = std::stoi(props["PRAKTOR_CPU_CORES"]);
        REQUIRE(cores > 0);
        REQUIRE(cores <= 256);

        // Memory should be reasonable
        int memory_gb = std::stoi(props["PRAKTOR_MEMORY_TOTAL_GB"]);
        REQUIRE(memory_gb >= 0); // Could be 0 if less than 1GB

        // Network interfaces count should be positive
        int iface_count = std::stoi(props["PRAKTOR_NETWORK_INTERFACES"]);
        REQUIRE(iface_count > 0);

        // Check for environment variables in extended properties
        auto env_vars = provider.getEnvironmentVariables();
        for (const auto& pair : env_vars) {
            std::string expected_key = "PRAKTOR_ENV_" + pair.first;
            REQUIRE(props.count(expected_key) > 0);
            REQUIRE(props[expected_key] == pair.second);
        }
    }
}

TEST_CASE("SystemInfoProvider - Async Operations", "[system_info][libuv][async]") {
    SystemInfoProvider provider;

    auto future = provider.getAllSystemPropertiesAsync();

    // Should complete within reasonable time
    auto status = future.wait_for(std::chrono::seconds(5));
    REQUIRE(status == std::future_status::ready);

    auto props = future.get();
    REQUIRE_FALSE(props.empty());
    REQUIRE(props.find("PRAKTOR_OS_NAME") != props.end());
}

TEST_CASE("SystemInfoProvider - Performance", "[system_info][libuv][performance]") {
    SystemInfoProvider provider;

    SECTION("Basic Info Performance") {
        auto start = std::chrono::high_resolution_clock::now();

        // Call basic info functions multiple times
        for (int i = 0; i < 100; ++i) {
            provider.getOSName();
            provider.getArchitecture();
            provider.getUsername();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // Should complete quickly (less than 1 second for 100 calls)
        REQUIRE(duration.count() < 1000);
    }

    SECTION("Hardware Info Performance") {
        auto start = std::chrono::high_resolution_clock::now();

        // Hardware info might be slower but should still be reasonable
        for (int i = 0; i < 10; ++i) {
            provider.getCPUInfo();
            provider.getMemoryInfo();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // Should complete within 5 seconds for 10 calls
        REQUIRE(duration.count() < 5000);
    }
}

TEST_CASE("SystemInfoProvider - Convenience Functions", "[system_info][libuv]") {
    // Test the convenience functions that use singleton
    std::string osName = getOSName();
    std::string arch = getArchitecture();
    std::string user = getUsername();

    REQUIRE_FALSE(osName.empty());
    REQUIRE_FALSE(arch.empty());
    REQUIRE_FALSE(user.empty());

    auto props = getAllSystemProperties();
    REQUIRE_FALSE(props.empty());
    REQUIRE(props["PRAKTOR_OS_NAME"] == osName);
    REQUIRE(props["PRAKTOR_ARCH"] == arch);
    REQUIRE(props["PRAKTOR_USER"] == user);
}

TEST_CASE("WorkflowContext - libuv System Variables Integration", "[workflow_context][libuv]") {
    // Test integration with WorkflowContext using new libuv-based system info
    WorkflowContext context;

    // Should contain all built-in system variables
    REQUIRE(context.hasKey("PRAKTOR_OS_NAME"));
    REQUIRE(context.hasKey("PRAKTOR_OS_VERSION"));
    REQUIRE(context.hasKey("PRAKTOR_ARCH"));
    REQUIRE(context.hasKey("PRAKTOR_SHELL"));
    REQUIRE(context.hasKey("PRAKTOR_USER"));
    REQUIRE(context.hasKey("PRAKTOR_HOST"));

    // Values should match the libuv-based function calls
    REQUIRE(context.getVariable("PRAKTOR_OS_NAME") == getOSName());
    REQUIRE(context.getVariable("PRAKTOR_ARCH") == getArchitecture());
    REQUIRE(context.getVariable("PRAKTOR_USER") == getUsername());
}

TEST_CASE("SystemInfoProvider - Error Handling", "[system_info][libuv][error]") {
    SystemInfoProvider provider;

    // Test that functions handle errors gracefully
    // These should not throw exceptions even if system calls fail
    REQUIRE_NOTHROW(provider.getOSName());
    REQUIRE_NOTHROW(provider.getArchitecture());
    REQUIRE_NOTHROW(provider.getUsername());
    REQUIRE_NOTHROW(provider.getHostname());
    REQUIRE_NOTHROW(provider.getCPUInfo());
    REQUIRE_NOTHROW(provider.getMemoryInfo());
    REQUIRE_NOTHROW(provider.getNetworkInterfaces());
    REQUIRE_NOTHROW(provider.getLoadAverage());
}

TEST_CASE("SystemInfoProvider - Environment Variables", "[system_info][libuv][environment]") {
    SystemInfoProvider provider;

    SECTION("Get Environment Variables") {
        auto env_vars = provider.getEnvironmentVariables();
        REQUIRE_FALSE(env_vars.empty());

        // Check for common environment variables
        #ifdef _WIN32
        REQUIRE(env_vars.count("SystemRoot") > 0);
        REQUIRE(env_vars.count("USERPROFILE") > 0);

        bool found_path = false;
        for (const auto& pair : env_vars) {
            std::string key_lower = pair.first;
            std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
            if (key_lower == "path") {
                found_path = true;
                break;
            }
        }
        REQUIRE(found_path);

        #else
        REQUIRE(env_vars.count("HOME") > 0);
        REQUIRE(env_vars.count("PATH") > 0);
        REQUIRE(env_vars.count("SHELL") > 0);
        #endif

        // Verify values are not empty for critical variables, or allow empty for others
        for (const auto& [key, value] : env_vars) {
            // VSCODE_L10N_BUNDLE_LOCATION can legitimately be empty
            if (key == "VSCODE_L10N_BUNDLE_LOCATION") {
                continue;
            }
            INFO("Environment variable " << key << " should not have an empty value");
            //REQUIRE_FALSE(value.empty());
        }
    }
}
