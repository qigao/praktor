/**
 * @file platform.h
 * @brief Provides platform-specific utilities and definitions.
 *
 * This header file handles platform-specific includes and definitions,
 * primarily for Windows and Unix-like systems. It ensures compatibility
 * and provides necessary headers for network and system operations
 * across different operating environments.
 */
#ifndef PLATFORM_UTILS_H
#define PLATFORM_UTILS_H

// Define NOMINMAX to prevent Windows.h from defining min/max macros
#ifdef _WIN32
  #define _WINSOCKAPI_
  #include <Ws2tcpip.h>  // For InetNtopA
  #include <windows.h>  // General Windows API
  #include <winsock2.h>  // Winsock API

  #pragma comment(lib, \
                  "ws2_32.lib")  // Link with ws2_32.lib for Winsock functions
  #undef max  // Undefine max macro to avoid conflicts with std::max
  #undef min  // Undefine min macro to avoid conflicts with std::min
  #define NOMINMAX  // Prevent Windows.h from defining min/max macros
  #define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers

  #include <psapi.h>  // For process information (if needed, though not directly used here)
  #define inet_ntop InetNtopA  // Map inet_ntop to its Windows equivalent

  #pragma comment(lib, "Ws2_32.lib")
  #pragma comment(lib, "ntdll.lib")
  #pragma comment(lib, "psapi.lib")
  #pragma comment(lib, "Iphlpapi.lib")
#else
  #include <sys/resource.h>  // For resource limits (e.g., setrlimit)
  #include <sys/stat.h>  // For file status (e.g., stat)
  #include <sys/types.h>  // For various data types (e.g., pid_t)
  #include <unistd.h>  // For POSIX operating system API (e.g., fork, exec, read, write)
#endif

// Add any cross-platform utility functions or types here if needed
// For example, you could add wrappers for platform-specific functions

#endif  // PLATFORM_UTILS_H
