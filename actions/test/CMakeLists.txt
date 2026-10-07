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

# The released macOS Salts SDK uses GCC's emulated TLS for TinyTest's public
# thread-local state. Keep Clang consumers on that ABI. Remove this local
# requirement when Salts::TinyTest exports the matching compile option.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    foreach(test_target IN LISTS actions_test_targets)
        target_compile_options(${test_target} PRIVATE -femulated-tls)
    endforeach()
endif()

target_compile_definitions(test_parallel_performance PRIVATE
    $<$<BOOL:WIN32>:NOMINMAX>
    $<$<BOOL:WIN32>:WIN32_LEAN_AND_MEAN>
)
