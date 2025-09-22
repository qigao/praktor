include(CMakeDependentOption)

set(CMAKE_COLOR_DIAGNOSTICS ON)

# building the tests
option(ENABLE_TESTS "Enable the tests" OFF)

# SSL support
option(ENABLE_SSL "Enable SSL support" ON)
cmake_dependent_option(
    USE_OPENSSL "Use OpenSSL" ON "ENABLE_SSL;NOT USE_MBEDTLS" OFF
)
cmake_dependent_option(
    USE_MBEDTLS "Use MbedTLS" OFF "ENABLE_SSL;NOT USE_OPENSSL" OFF
)

if(ENABLE_SSL)
    if(USE_OPENSSL)
        set(SSL_BACKEND_USED "OpenSSL")
    elseif(USE_MBEDTLS)
        set(SSL_BACKEND_USED "MbedTLS")
    else()
        message(
            FATAL_ERROR
                "No valid SSL backend selected. Please enable either USE_OPENSSL or USE_MBEDTLS."
        )
    endif()
endif()
message(STATUS "SSL backend used: ${SSL_BACKEND_USED}")
# if(MSVC)
#     add_compile_options(/bigobj)
# endif()

# zlib support
option(ENABLE_ZLIB "Use zlib" ON)

option(ENABLE_GIT_INFO "List git status" OFF)
option(ENABLE_CROSS_COMPILING "Detect cross compiler and setup toolchain" ON)
option(ENABLE_SAMPLES "Enable sample projects" ON)
option(BUILD_SHARED_LIBS "Build shared instead of static libraries." ON)

# Polly Plugin System Options
option(POLLY_ENABLE_LEGACY_DLL "Enable legacy DLL plugin support" ON)
option(POLLY_ENABLE_SCHEMA_DLL "Enable schema-based native DLL plugin support" ON)
option(POLLY_ENABLE_LUA "Enable Lua plugin support" ON)
option(POLLY_ENABLE_WASM "Enable WebAssembly plugin support" ON)

# Polly Compilation Mode Options
option(POLLY_ENABLE_AOT "Enable Ahead-of-Time compilation support" ON)
option(POLLY_ENABLE_JIT "Enable Just-in-Time compilation support" ON)

# Plugin system feature validation
if(NOT POLLY_ENABLE_LEGACY_DLL AND NOT POLLY_ENABLE_SCHEMA_DLL AND NOT POLLY_ENABLE_LUA AND NOT POLLY_ENABLE_WASM)
    message(FATAL_ERROR "At least one plugin type must be enabled")
endif()

# Compilation mode validation
if(NOT POLLY_ENABLE_AOT AND NOT POLLY_ENABLE_JIT)
    message(FATAL_ERROR "At least one compilation mode (AOT or JIT) must be enabled")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

find_package(Threads REQUIRED)
