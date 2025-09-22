#ifndef MULTI_PLATFORM_NET_UTILS_H
#define MULTI_PLATFORM_NET_UTILS_H

// ==========================================================================
// == Platform Detection & Core OS/Networking Includes                    ==
// ==========================================================================
#ifdef _WIN32
  #pragma warning(push)
  #pragma warning(disable : 4081)
  #pragma warning(disable : 4706)
  #pragma warning(disable : 4996)
  // system header includes
  #pragma warning(pop)
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  // Prevent definition of min/max macros by <windows.h>
  // avoids conflicts with std::min/std::max and other libraries.
  #include <Ws2tcpip.h>
  #include <windows.h>
  #include <winsock2.h>
  #pragma comment(lib, "ws2_32.lib")
  #undef max
  #undef min

  // MSVC doesn't define ssize_t, which is a POSIX type used by lsquic.
  #include <BaseTsd.h>
typedef SSIZE_T ssize_t;

  #define inet_ntop InetNtopA

  #pragma comment(lib, "Ws2_32.lib")
  #pragma comment(lib, "ntdll.lib")

#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__) \
    || defined(__ANDROID__)
  // --- Unix-like (Linux, macOS, BSD, etc.) ---

  #include <arpa/inet.h>  // inet_ntop, inet_pton, etc.
  #include <errno.h>  // For errno (error reporting)
  #include <fcntl.h>  // For non-blocking sockets (fcntl)
  #include <netdb.h>  // getaddrinfo, gethostbyname, etc.
  #include <netinet/in.h>  // sockaddr_in, IPPROTO_*, etc.
  #include <sys/socket.h>  // Core Berkeley sockets
  #include <unistd.h>  // POSIX API (read, write, close, etc.)

#else
  #error "Unsupported platform detected. Please add includes for your platform."

  #if defined(__ANDROID__)
    // Android includes (likely JNI related if doing Keystore access)
    #include <jni.h>
  #endif

#endif

#endif  // MULTI_PLATFORM_NET_UTILS_H
