string(REPLACE "\\" "\\\\" PYTHON3_EXECUTABLE_ESCAPED "${Python3_EXECUTABLE}")
string(REPLACE "\\" "\\\\" FORCE_TERMINATE_CORPUS_ESCAPED
       "${CMAKE_CURRENT_SOURCE_DIR}/data/force_terminate_corpus.tsv")
string(REPLACE "\\" "\\\\" SYSTEM_ACTION_STRING_CORPUS_ESCAPED
       "${CMAKE_CURRENT_SOURCE_DIR}/data/system_action_string_corpus.tsv")

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

target_compile_definitions(task_parser_test.${TARGET_NAME_LIB} PRIVATE
    PRAKTOR_FORCE_TERMINATE_CORPUS="${FORCE_TERMINATE_CORPUS_ESCAPED}"
    PRAKTOR_SYSTEM_ACTION_STRING_CORPUS="${SYSTEM_ACTION_STRING_CORPUS_ESCAPED}"
)

target_include_directories(
    workflow_runner_test.${TARGET_NAME_LIB}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

if(WIN32)
    add_executable(managed_process_fixture helpers/managed_process_fixture.cpp)
    add_executable(managed_process_fixture_alt helpers/managed_process_fixture.cpp)
    set_target_properties(managed_process_fixture_alt PROPERTIES
        OUTPUT_NAME "managed_process_fixture"
        SUFFIX ".com"
    )
    cmake_config_target(
        managed_process_fixture
        NO_INSTALL
        NO_VERSION
        FOLDER "${folder_name}/test/helpers"
    )
    cmake_config_target(
        managed_process_fixture_alt
        NO_INSTALL
        NO_VERSION
        FOLDER "${folder_name}/test/helpers"
    )
    add_dependencies(managed_process_executor_test.${TARGET_NAME_LIB}
                     managed_process_fixture
                     managed_process_fixture_alt)
    add_dependencies(workflow_runner_test.${TARGET_NAME_LIB}
                     managed_process_fixture)
    target_compile_definitions(
        managed_process_executor_test.${TARGET_NAME_LIB}
        PRIVATE PRAKTOR_MANAGED_PROCESS_FIXTURE="$<TARGET_FILE:managed_process_fixture>"
                PRAKTOR_MANAGED_PROCESS_FIXTURE_ALT="$<TARGET_FILE:managed_process_fixture_alt>"
    )
    target_compile_definitions(
        workflow_runner_test.${TARGET_NAME_LIB}
        PRIVATE PRAKTOR_MANAGED_PROCESS_FIXTURE="$<TARGET_FILE:managed_process_fixture>"
    )
    set_tests_properties(
        managed_process_executor_test.${TARGET_NAME_LIB}
        workflow_runner_test.${TARGET_NAME_LIB}
        PROPERTIES RESOURCE_LOCK praktor_managed_process_fixture
    )
endif()

add_test(
    NAME schema_validation_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/schema_validation_test.py
)
