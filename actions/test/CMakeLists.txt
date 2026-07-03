cmake_minimum_required(VERSION 3.16)

# Find TinyTest
find_package(TurboNet CONFIG REQUIRED)

add_executable(test_ast test_ast.cpp)
target_link_libraries(test_ast PRIVATE actions TurboNet::TinyTest)
target_compile_features(test_ast PRIVATE cxx_std_17)
add_test(NAME test_ast COMMAND test_ast)

add_executable(test_advanced_decorators test_advanced_decorators.cpp)
target_link_libraries(test_advanced_decorators PRIVATE actions TurboNet::TinyTest)
target_compile_features(test_advanced_decorators PRIVATE cxx_std_17)
add_test(NAME test_advanced_decorators COMMAND test_advanced_decorators)

add_executable(test_thread_pool test_thread_pool.cpp)
target_link_libraries(test_thread_pool PRIVATE actions TurboNet::TinyTest)
target_compile_features(test_thread_pool PRIVATE cxx_std_17)
add_test(NAME test_thread_pool COMMAND test_thread_pool)

add_executable(test_parallel_performance test_parallel_performance.cpp)
target_link_libraries(test_parallel_performance PRIVATE actions TurboNet::TinyTest)
target_compile_features(test_parallel_performance PRIVATE cxx_std_17)
if(WIN32)
    target_compile_definitions(
   test_parallel_performance PUBLIC $<$<BOOL:WIN32>:NOMINMAX>
                              $<$<BOOL:WIN32>:WIN32_LEAN_AND_MEAN>
)
endif(WIN32)
add_test(NAME test_parallel_performance COMMAND test_parallel_performance)

add_executable(test_extended_nodes test_extended_nodes.cpp)
target_link_libraries(test_extended_nodes PRIVATE actions TurboNet::TinyTest)
target_compile_features(test_extended_nodes PRIVATE cxx_std_17)
add_test(NAME test_extended_nodes COMMAND test_extended_nodes)

add_executable(test_monitor test_monitor.cpp)
target_link_libraries(test_monitor PRIVATE actions TurboNet::TinyTest)
target_compile_features(test_monitor PRIVATE cxx_std_17)
add_test(NAME test_monitor COMMAND test_monitor)

# Set test properties
set_target_properties(
    test_ast
    test_advanced_decorators
    test_thread_pool
    test_parallel_performance
    test_extended_nodes
    test_monitor
    PROPERTIES FOLDER "actions/tests"
)
