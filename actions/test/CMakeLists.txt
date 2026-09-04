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
)

target_compile_definitions(test_parallel_performance PRIVATE
    $<$<BOOL:WIN32>:NOMINMAX>
    $<$<BOOL:WIN32>:WIN32_LEAN_AND_MEAN>
)
