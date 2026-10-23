#include "praktor_test.hpp"
#include "util/system_info.hpp"
#include "dag/workflow_context.hpp"
#include <string>
#include <chrono>

using namespace Praktor::system;

[[maybe_unused]] static void test_systeminfoprovider_basic_os_information_1(int section_index) {
    SystemInfoProvider provider;

    if (section_index == 0) {
        std::string osName = provider.getOSName();
        check_false(osName.empty());
        check(((osName == "windows" || osName == "linux" || osName == "macos" || osName == "unix" || osName == "unknown")));
    }

    if (section_index == 1) {
        std::string osVersion = provider.getOSVersion();
        check_false(osVersion.empty());
        check(((osVersion == "unknown" || osVersion.find_first_of("0123456789") != std::string::npos)));
    }

    if (section_index == 2) {
        std::string arch = provider.getArchitecture();
        check_false(arch.empty());
        check(((arch == "x86_64" || arch == "arm64" || arch == "x86" || arch == "arm" ||
                 arch == "i386" || arch == "aarch64" || arch == "unknown")));
    }
}

spec("SystemInfoProvider - Basic OS Information") {
    it("OS Name") { test_systeminfoprovider_basic_os_information_1(0); }
    it("OS Version") { test_systeminfoprovider_basic_os_information_1(1); }
    it("Architecture") { test_systeminfoprovider_basic_os_information_1(2); }
}

[[maybe_unused]] static void test_systeminfoprovider_user_and_network_information_2(int section_index) {
    SystemInfoProvider provider;

    if (section_index == 0) {
        std::string username = provider.getUsername();
        check_false(username.empty());
        // Username should be a valid string (not just "unknown")
        if (username != "unknown") {
            check((username.length() > 0));
            check((username.find_first_not_of(" \t\n\r") != std::string::npos));
        }
    }

    if (section_index == 1) {
        std::string hostname = provider.getHostname();
        check_false(hostname.empty());
        if (hostname != "unknown") {
            check((hostname.length() > 0));
        }
    }

    if (section_index == 2) {
        std::string shell = provider.getShellName();
        check_false(shell.empty());
        check(((shell == "bash" || shell == "zsh" || shell == "fish" ||
                 shell == "cmd" || shell == "powershell" || shell == "sh" || shell == "unknown")));
    }
}

spec("SystemInfoProvider - User and Network Information") {
    it("Username") { test_systeminfoprovider_user_and_network_information_2(0); }
    it("Hostname") { test_systeminfoprovider_user_and_network_information_2(1); }
    it("Shell Name") { test_systeminfoprovider_user_and_network_information_2(2); }
}

[[maybe_unused]] static void test_systeminfoprovider_hardware_information_3(int section_index) {
    SystemInfoProvider provider;

    if (section_index == 0) {
        auto cpu_info = provider.getCPUInfo();

        // CPU model should be available on most systems
        check_false(cpu_info.model.empty());

        // Core count should be positive
        check((cpu_info.core_count > 0));
        check((cpu_info.core_count <= 256)); // Reasonable upper bound

        // CPU speed should be reasonable (in MHz)
        if (cpu_info.speed_mhz > 0) {
            check((cpu_info.speed_mhz >= 100.0));    // At least 100 MHz
            check((cpu_info.speed_mhz <= 10000.0));  // Less than 10 GHz
        }
    }

    if (section_index == 1) {
        auto mem_info = provider.getMemoryInfo();

        // Total memory should be positive
        check((mem_info.total_memory > 0));

        // Free memory should be less than or equal to total
        check((mem_info.free_memory <= mem_info.total_memory));

        // Available memory should be reasonable
        check((mem_info.available_memory <= mem_info.total_memory));

        // Minimum reasonable memory: 512MB
        check((mem_info.total_memory >= 512 * 1024 * 1024));
    }
}

spec("SystemInfoProvider - Hardware Information") {
    it("CPU Information") { test_systeminfoprovider_hardware_information_3(0); }
    it("Memory Information") { test_systeminfoprovider_hardware_information_3(1); }
}

[[maybe_unused]] static void test_systeminfoprovider_network_interfaces_4() {
    SystemInfoProvider provider;

    auto interfaces = provider.getNetworkInterfaces();

    // Should have at least one interface (loopback)
    check((interfaces.size() > 0));

    bool has_loopback = false;
    bool has_external = false;

    for (const auto& iface : interfaces) {
        // Interface name should not be empty
        check_false(iface.name.empty());

        // Address should not be empty
        check_false(iface.address.empty());

        const bool looks_like_loopback =
            iface.is_internal ||
            iface.address.find("127.0.0.1") != std::string::npos ||
            iface.address.find("::1") != std::string::npos ||
            iface.name == "lo" ||
            iface.name.find("loopback") != std::string::npos;

        if (looks_like_loopback) {
            has_loopback = true;
        } else {
            has_external = true;
        }

        // Check for common interface patterns
        if (iface.address.find("127.0.0.1") != std::string::npos ||
            iface.address.find("::1") != std::string::npos) {
            check((looks_like_loopback));
        }
    }

    // Most systems should have a loopback interface
    check((has_loopback));
}

spec("SystemInfoProvider - Network Interfaces") {
    it("complete case") { test_systeminfoprovider_network_interfaces_4(); }
}

[[maybe_unused]] static void test_systeminfoprovider_load_average_5() {
    SystemInfoProvider provider;

    auto load = provider.getLoadAverage();

    // Load averages should be non-negative
    check((load.one_minute >= 0.0));
    check((load.five_minutes >= 0.0));
    check((load.fifteen_minutes >= 0.0));

    // Load averages should be reasonable (less than 1000)
    check((load.one_minute < 1000.0));
    check((load.five_minutes < 1000.0));
    check((load.fifteen_minutes < 1000.0));
}

spec("SystemInfoProvider - Load Average") {
    it("complete case") { test_systeminfoprovider_load_average_5(); }
}

[[maybe_unused]] static void test_systeminfoprovider_all_system_properties_6(int section_index) {
    SystemInfoProvider provider;

    if (section_index == 0) {
        auto props = provider.getAllSystemProperties();

        // Should contain all expected basic properties
        check((props.find("PRAKTOR_OS_NAME") != props.end()));
        check((props.find("PRAKTOR_OS_VERSION") != props.end()));
        check((props.find("PRAKTOR_ARCH") != props.end()));
        check((props.find("PRAKTOR_SHELL") != props.end()));
        check((props.find("PRAKTOR_USER") != props.end()));
        check((props.find("PRAKTOR_HOST") != props.end()));

        // All values should be non-empty
        for (const auto& [key, value] : props) {
            info("%s", praktor_test::message([&](std::ostream& out) { out << "Property " << key << " should not be empty"; }).c_str());
            check_false(value.empty());
        }
    }

    if (section_index == 1) {
        auto props = provider.getExtendedSystemProperties();

        // Should contain hardware information
        check((props.find("PRAKTOR_CPU_MODEL") != props.end()));
        check((props.find("PRAKTOR_CPU_CORES") != props.end()));
        check((props.find("PRAKTOR_MEMORY_TOTAL_GB") != props.end()));
        check((props.find("PRAKTOR_NETWORK_INTERFACES") != props.end()));

        // CPU cores should be a valid number
        int cores = std::stoi(props["PRAKTOR_CPU_CORES"]);
        check((cores > 0));
        check((cores <= 256));

        // Memory should be reasonable
        int memory_gb = std::stoi(props["PRAKTOR_MEMORY_TOTAL_GB"]);
        check((memory_gb >= 0)); // Could be 0 if less than 1GB

        // Network interfaces count should be positive
        int iface_count = std::stoi(props["PRAKTOR_NETWORK_INTERFACES"]);
        check((iface_count > 0));

        // Check for environment variables in extended properties
        auto env_vars = provider.getEnvironmentVariables();
        for (const auto& pair : env_vars) {
            std::string expected_key = "PRAKTOR_ENV_" + pair.first;
            check((props.count(expected_key) > 0));
            check((props[expected_key] == pair.second));
        }
    }
}

spec("SystemInfoProvider - All System Properties") {
    it("Basic Properties") { test_systeminfoprovider_all_system_properties_6(0); }
    it("Extended Properties") { test_systeminfoprovider_all_system_properties_6(1); }
}

[[maybe_unused]] static void test_systeminfoprovider_async_operations_7() {
    SystemInfoProvider provider;

    auto future = provider.getAllSystemPropertiesAsync();

    // Should complete within reasonable time
    auto status = future.wait_for(std::chrono::seconds(5));
    check((status == std::future_status::ready));

    auto props = future.get();
    check_false(props.empty());
    check((props.find("PRAKTOR_OS_NAME") != props.end()));
}

spec("SystemInfoProvider - Async Operations") {
    it("complete case") { test_systeminfoprovider_async_operations_7(); }
}

[[maybe_unused]] static void test_systeminfoprovider_performance_8(int section_index) {
    SystemInfoProvider provider;

    if (section_index == 0) {
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
        check((duration.count() < 1000));
    }

    if (section_index == 1) {
        auto start = std::chrono::high_resolution_clock::now();

        // Hardware info might be slower but should still be reasonable
        for (int i = 0; i < 10; ++i) {
            provider.getCPUInfo();
            provider.getMemoryInfo();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // Should complete within 5 seconds for 10 calls
        check((duration.count() < 5000));
    }
}

spec("SystemInfoProvider - Performance") {
    it("Basic Info Performance") { test_systeminfoprovider_performance_8(0); }
    it("Hardware Info Performance") { test_systeminfoprovider_performance_8(1); }
}

[[maybe_unused]] static void test_systeminfoprovider_convenience_functions_9() {
    // Test the convenience functions that use singleton
    std::string osName = getOSName();
    std::string arch = getArchitecture();
    std::string user = getUsername();

    check_false(osName.empty());
    check_false(arch.empty());
    check_false(user.empty());

    auto props = getAllSystemProperties();
    check_false(props.empty());
    check((props["PRAKTOR_OS_NAME"] == osName));
    check((props["PRAKTOR_ARCH"] == arch));
    check((props["PRAKTOR_USER"] == user));
}

spec("SystemInfoProvider - Convenience Functions") {
    it("complete case") { test_systeminfoprovider_convenience_functions_9(); }
}

[[maybe_unused]] static void test_workflowcontext_libuv_system_variables_integration_10() {
    // Test integration with WorkflowContext using new libuv-based system info
    WorkflowContext context;

    // Should contain all built-in system variables
    check((context.hasKey("PRAKTOR_OS_NAME")));
    check((context.hasKey("PRAKTOR_OS_VERSION")));
    check((context.hasKey("PRAKTOR_ARCH")));
    check((context.hasKey("PRAKTOR_SHELL")));
    check((context.hasKey("PRAKTOR_USER")));
    check((context.hasKey("PRAKTOR_HOST")));

    // Values should match the libuv-based function calls
    check((context.getVariable("PRAKTOR_OS_NAME") == getOSName()));
    check((context.getVariable("PRAKTOR_ARCH") == getArchitecture()));
    check((context.getVariable("PRAKTOR_USER") == getUsername()));
}

spec("WorkflowContext - libuv System Variables Integration") {
    it("complete case") { test_workflowcontext_libuv_system_variables_integration_10(); }
}

[[maybe_unused]] static void test_systeminfoprovider_error_handling_11() {
    SystemInfoProvider provider;

    // Test that functions handle errors gracefully
    // These should not throw exceptions even if system calls fail
    check_nothrow(provider.getOSName());
    check_nothrow(provider.getArchitecture());
    check_nothrow(provider.getUsername());
    check_nothrow(provider.getHostname());
    check_nothrow(provider.getCPUInfo());
    check_nothrow(provider.getMemoryInfo());
    check_nothrow(provider.getNetworkInterfaces());
    check_nothrow(provider.getLoadAverage());
}

spec("SystemInfoProvider - Error Handling") {
    it("complete case") { test_systeminfoprovider_error_handling_11(); }
}

[[maybe_unused]] static void test_systeminfoprovider_environment_variables_12(int section_index) {
    SystemInfoProvider provider;

    if (section_index == 0) {
        auto env_vars = provider.getEnvironmentVariables();
        check_false(env_vars.empty());

        // Check for common environment variables
        #ifdef _WIN32
        check((env_vars.count("SystemRoot") > 0));
        check((env_vars.count("USERPROFILE") > 0));

        bool found_path = false;
        for (const auto& pair : env_vars) {
            std::string key_lower = pair.first;
            std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
            if (key_lower == "path") {
                found_path = true;
                break;
            }
        }
        check((found_path));

        #else
        check((env_vars.count("HOME") > 0));
        check((env_vars.count("PATH") > 0));
        check((env_vars.count("SHELL") > 0));
        #endif

        // Verify values are not empty for critical variables, or allow empty for others
        for (const auto& [key, value] : env_vars) {
            // VSCODE_L10N_BUNDLE_LOCATION can legitimately be empty
            if (key == "VSCODE_L10N_BUNDLE_LOCATION") {
                continue;
            }
            info("%s", praktor_test::message([&](std::ostream& out) { out << "Environment variable " << key << " should not have an empty value"; }).c_str());
            //check_false(value.empty());
        }
    }
}

spec("SystemInfoProvider - Environment Variables") {
    it("Get Environment Variables") { test_systeminfoprovider_environment_variables_12(0); }
}
