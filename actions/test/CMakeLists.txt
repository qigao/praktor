cmake_add_test(
    SOURCES
        test_ast.cpp
        test_advanced_decorators.cpp
        test_thread_pool.cpp
        test_parallel_performance.cpp
        test_extended_nodes.cpp
        test_monitor.cpp
    LIBS actions Salts::TinyTest
    FEATURES cxx_std_17
    FOLDER "actions/tests"
    OUT_TARGETS actions_test_targets
)

# Older macOS SDKs used GCC's emulated TLS; newer SDKs use native TLS.
# Probe the linked runtime instead of imposing the older ABI on every SDK.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_LIBRARIES Salts::TinyTest)
    set(_tinytest_probe "#include <tinytest.hpp>\nspec(\"TLS ABI\") { it(\"links\") { check(true); } }")
    # SDK roots can change in an existing build directory.
    unset(PRAKTOR_TINYTEST_NATIVE_TLS CACHE)
    check_cxx_source_compiles("${_tinytest_probe}" PRAKTOR_TINYTEST_NATIVE_TLS)
    if(NOT PRAKTOR_TINYTEST_NATIVE_TLS)
        set(CMAKE_REQUIRED_FLAGS "-femulated-tls")
        unset(PRAKTOR_TINYTEST_EMULATED_TLS CACHE)
        check_cxx_source_compiles("${_tinytest_probe}" PRAKTOR_TINYTEST_EMULATED_TLS)
        if(NOT PRAKTOR_TINYTEST_EMULATED_TLS)
            message(FATAL_ERROR "Salts::TinyTest cannot link with native or emulated TLS")
        endif()
        foreach(test_target IN LISTS actions_test_targets)
            target_compile_options(${test_target} PRIVATE -femulated-tls)
        endforeach()
    endif()
    cmake_pop_check_state()
    unset(_tinytest_probe)
endif()

target_compile_definitions(test_parallel_performance PRIVATE
    $<$<BOOL:WIN32>:NOMINMAX>
    $<$<BOOL:WIN32>:WIN32_LEAN_AND_MEAN>
)
