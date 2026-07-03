find_package(Catch2 CONFIG REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
string(REPLACE "\\" "\\\\" PYTHON3_EXECUTABLE_ESCAPED "${Python3_EXECUTABLE}")

set(TEST_SOURCES
    dynamic_tasks_executor_test.cpp
    expression_evaluator_test.cpp
    graph_test.cpp
    system_info_test.cpp
    task_parser_test.cpp
    task_registry_test.cpp
    concurrency_test.cpp
    thread_pool_test.cpp
    trigger_parser_test.cpp
    workflow_context_test.cpp
    workflow_runner_test.cpp
    core_improvements_test.cpp
    matrix_test.cpp
    native_loader_test.cpp
    script_engine_test.cpp
    declarative_tree_executor_test.cpp
)

# TinyTest-based tests (separate list)
set(TINYTEST_SOURCES
)

# Copy test fixtures to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
     DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# Catch2 tests
foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    set(executable_name "${test_name}.${TARGET_NAME_LIB}")
    add_executable(${executable_name} ${test_source})
    target_link_libraries(${executable_name} PRIVATE Catch2::Catch2WithMain ${TARGET_NAME_LIB})
    if(test_name STREQUAL "script_engine_test")
        target_compile_definitions(${executable_name} PRIVATE PRAKTOR_TEST_PYTHON="${PYTHON3_EXECUTABLE_ESCAPED}")
    endif()
    praktor_copy_runtime_artifacts(${executable_name})
    add_test(NAME ${executable_name} COMMAND ${executable_name})
    set_target_properties(${executable_name} PROPERTIES FOLDER ${folder_name}/test)
endforeach()

# TinyTest tests
foreach(test_source ${TINYTEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    set(executable_name "${test_name}.${TARGET_NAME_LIB}")
    add_executable(${executable_name} ${test_source})
    target_link_libraries(${executable_name} PRIVATE TurboNet::TinyTest ${TARGET_NAME_LIB})
    praktor_copy_runtime_artifacts(${executable_name})
    add_test(NAME ${executable_name} COMMAND ${executable_name})
    set_target_properties(${executable_name} PROPERTIES FOLDER ${folder_name}/test)
endforeach()

add_test(
    NAME schema_validation_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/schema_validation_test.py
)
