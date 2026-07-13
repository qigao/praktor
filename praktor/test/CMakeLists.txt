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

# Copy test fixtures to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
     DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

cmake_add_test(
    SOURCES ${TEST_SOURCES}
    LIBS Catch2::Catch2WithMain ${TARGET_NAME_LIB}
    TARGET_SUFFIX ".${TARGET_NAME_LIB}"
    FOLDER "${folder_name}/test"
    OUT_TARGETS PRAKTOR_TEST_TARGETS
)

target_compile_definitions(script_engine_test.${TARGET_NAME_LIB} PRIVATE
    PRAKTOR_TEST_PYTHON="${PYTHON3_EXECUTABLE_ESCAPED}"
)

foreach(test_target ${PRAKTOR_TEST_TARGETS})
    praktor_copy_runtime_artifacts(${test_target})
endforeach()

add_test(
    NAME schema_validation_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/schema_validation_test.py
)
