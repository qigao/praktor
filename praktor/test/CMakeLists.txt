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
    script_engine_test.cpp
    declarative_tree_executor_test.cpp
    download_executor_test.cpp
    service_executor_test.cpp
    managed_process_executor_test.cpp
    structured_document_query_test.cpp
)

cmake_add_test(
    SOURCES ${TEST_SOURCES}
    LIBS Catch2::Catch2WithMain ${TARGET_NAME_LIB}
    TARGET_SUFFIX ".${TARGET_NAME_LIB}"
    FOLDER "${folder_name}/test"
    OUT_TARGETS PRAKTOR_TEST_TARGETS
)

if(WIN32)
    foreach(test_target IN LISTS PRAKTOR_TEST_TARGETS)
        add_custom_command(
            TARGET ${test_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:${test_target}>
                    "$<TARGET_FILE_DIR:${test_target}>"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    endforeach()
endif()

target_compile_definitions(script_engine_test.${TARGET_NAME_LIB} PRIVATE
    PRAKTOR_TEST_PYTHON="${PYTHON3_EXECUTABLE_ESCAPED}"
)

if(WIN32)
    add_executable(managed_process_fixture helpers/managed_process_fixture.cpp)
    cmake_config_target(
        managed_process_fixture
        NO_INSTALL
        NO_VERSION
        FOLDER "${folder_name}/test/helpers"
    )
    add_dependencies(managed_process_executor_test.${TARGET_NAME_LIB}
                     managed_process_fixture)
    target_compile_definitions(
        managed_process_executor_test.${TARGET_NAME_LIB}
        PRIVATE PRAKTOR_MANAGED_PROCESS_FIXTURE="$<TARGET_FILE:managed_process_fixture>"
    )
endif()

add_test(
    NAME schema_validation_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/schema_validation_test.py
)
