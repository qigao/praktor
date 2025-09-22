#include "catch2/catch_test_macros.hpp"

// Include the header with the functions to be tested
#include "pubcxx/utils.hpp"

#include <filesystem>   // For std::filesystem::path and related checks (C++17)
#include <iostream>     // For potential debug output
#include <string>

// Helper to check if a path string looks like an absolute path
// This is a basic check and might need refinement for edge cases on different OSes
bool is_potentially_absolute(std::string const& path_str) {
    if (path_str.empty()) { return false; }
#ifdef _WIN32
    // e.g., C:\, \server\share
    if (path_str.length() >= 2 && path_str[1] == ':') { return true; }
    if (path_str.length() >= 2 && path_str[0] == '\\' && path_str[1] == '\\') { return true; }
#else
    // e.g., /home/user
    if (path_str[0] == '/') { return true; }
#endif
    return false;
}

TEST_CASE("Executable Path Utilities", "[utils][filesystem]") {
    namespace fs = std::filesystem;

    SECTION("getCurrentExecutablePath") {
        std::string exePath;
        REQUIRE_NOTHROW(exePath = getCurrentExecutablePath());

        INFO("Reported executable path: " << exePath);
        REQUIRE_FALSE(exePath.empty());

        // Check if it's an absolute path using std::filesystem
        // std::filesystem::path constructor can throw if the path format is invalid for the OS,
        // but getCurrentExecutablePath is expected to return a valid format.
        fs::path fsExePath;
        REQUIRE_NOTHROW(fsExePath = fs::path(exePath));

        // Use our helper for a basic check, and then std::filesystem for a more robust one.
        CHECK(is_potentially_absolute(exePath));
        REQUIRE(fsExePath.is_absolute());

        // Check if the path reported by the function actually exists and is a file
        // This assumes the test executable itself is a regular file.
        REQUIRE(fs::exists(fsExePath));
        REQUIRE(fs::is_regular_file(fsExePath));

        // Optional: Check if the filename is not empty
        REQUIRE_FALSE(fsExePath.filename().empty());
    }

    SECTION("getCurrentExecutableDirectory") {
        std::string exeDir;
        REQUIRE_NOTHROW(exeDir = getCurrentExecutableDirectory());

        INFO("Reported executable directory: " << exeDir);

        // The directory might be empty if the executable path was just a filename
        // (though getCurrentExecutablePath aims to return full paths).
        // However, with the current implementation of getCurrentExecutablePath,
        // exeDir should typically not be empty.
        // If exePath is "C:\foo\bar.exe", exeDir is "C:\foo".
        // If exePath is "/foo/bar.exe", exeDir is "/foo".
        // If exePath is "bar.exe" (less likely from the APIs used), exeDir is "".

        std::string exePathFull;
        REQUIRE_NOTHROW(exePathFull = getCurrentExecutablePath());
        fs::path fsExePathFull(exePathFull);

        if (exeDir.empty()) {
            // This case implies the executable path had no directory separators.
            // This is unlikely for the APIs used in getCurrentExecutablePath.
            // fs::path("filename.exe").parent_path() is also empty.
            INFO("Executable directory is empty. This implies the executable path had no directory component.");
            REQUIRE(fsExePathFull.parent_path().empty());
        } else {
            REQUIRE_FALSE(exeDir.empty());

            fs::path fsExeDir;
            REQUIRE_NOTHROW(fsExeDir = fs::path(exeDir));

            CHECK(is_potentially_absolute(exeDir));   // Directory should also be absolute if path was.
            REQUIRE(fsExeDir.is_absolute());

            REQUIRE(fs::exists(fsExeDir));
            REQUIRE(fs::is_directory(fsExeDir));

            // Verify consistency: the directory returned should be the parent path of the full executable path
            REQUIRE(fsExePathFull.parent_path() == fsExeDir);
        }
    }

    SECTION("Consistency between path and directory") {
        std::string exePath;
        std::string exeDir;

        REQUIRE_NOTHROW(exePath = getCurrentExecutablePath());
        REQUIRE_NOTHROW(exeDir = getCurrentExecutableDirectory());

        if (!exeDir.empty()) {
            fs::path fsExePath(exePath);
            fs::path fsExeDir(exeDir);
            REQUIRE(fsExePath.parent_path() == fsExeDir);
        } else {
            // If exeDir is empty, it means getCurrentExecutablePath likely returned a path
            // without directory separators (e.g., just "program.exe").
            // In this case, fs::path("program.exe").parent_path() is also empty.
            fs::path fsExePath(exePath);
            REQUIRE(fsExePath.parent_path().empty());
        }
    }
}
