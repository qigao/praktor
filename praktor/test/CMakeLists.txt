include(${CMAKE_CURRENT_SOURCE_DIR}/../src/expressions/lexer_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../src/expressions/evaluator_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../src/data/query_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../src/data/value_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../src/data/workflow_contract_value_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../src/yml/yaml_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../src/yml/yaml_value_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../src/yml/yaml_contract_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../src/yml/yaml_workflow_metadata_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../src/yml/yaml_task_metadata_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../src/yml/yaml_action_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../src/yml/yaml_task_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../src/yml/orch_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../src/yml/orch_yaml_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../src/profile/profile_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../src/util/file_hash_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../src/util/praktor_init_tests.cmake)

# Compile this public header without a transitive include of the
# legacy actions Executor. The main target links actions privately, so this
# isolated consumer has only Praktor's declared public include interface.
add_library(praktor_public_header_contract OBJECT
    c11_public_header_contract.cpp)
target_link_libraries(praktor_public_header_contract PRIVATE ${TARGET_NAME_LIB})
target_compile_features(praktor_public_header_contract PRIVATE cxx_std_17)
set_target_properties(praktor_public_header_contract PROPERTIES
    FOLDER "praktor/test/contracts")

if(ENABLE_SCRIPT_ENGINE)
    string(REPLACE "\\" "\\\\" PYTHON3_EXECUTABLE_ESCAPED "${Python3_EXECUTABLE}")
    string(REPLACE "\\" "\\\\" FORCE_TERMINATE_CORPUS_ESCAPED
           "${CMAKE_CURRENT_SOURCE_DIR}/data/force_terminate_corpus.tsv")
    string(REPLACE "\\" "\\\\" SYSTEM_ACTION_STRING_CORPUS_ESCAPED
           "${CMAKE_CURRENT_SOURCE_DIR}/data/system_action_string_corpus.tsv")

    set(TEST_SOURCES
        dynamic_tasks_executor_test.cpp
        graph_test.cpp
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
    )
else()
    set(TEST_SOURCES core_only_workflow_test.cpp)
endif()

list(APPEND TEST_SOURCES
    workflow_value_test.cpp
    structured_document_query_test.cpp
    orch_yaml_differential_test.cpp
    expression_evaluator_test.cpp
    service_executor_test.cpp
    managed_process_executor_test.cpp
    download_executor_test.cpp
    workflow_execution_regression_test.cpp
    system_info_test.cpp
    trigger_dependency_once_test.cpp
)

# Same YAML independently exercises production C++ WorkflowRunner and
# source-only C11 Snapshot/DAG/Session/CFlow Owner. No backend switch.
if(PRAKTOR_ENABLE_C11_ACTIONS)
    list(APPEND TEST_SOURCES workflow_owner_differential_test.cpp)
endif()

cmake_add_test(
    SOURCES ${TEST_SOURCES}
    LIBS praktor_test_support ${TARGET_NAME_LIB}
    TARGET_SUFFIX ".${TARGET_NAME_LIB}"
    FOLDER "${folder_name}/test"
    OUT_TARGETS PRAKTOR_TEST_TARGETS
)

# This test includes the internal native YAML action projection contract.
target_link_libraries(structured_document_query_test.${TARGET_NAME_LIB}
    PRIVATE praktor_yaml_action_params)
target_link_libraries(orch_yaml_differential_test.${TARGET_NAME_LIB}
    PRIVATE praktor_orch_yaml
            praktor_yaml_task_metadata
            praktor_yaml_workflow_metadata
            praktor_workflow_dependencies
            praktor_workflow_snapshot
            praktor_workflow_task_registry)
target_include_directories(orch_yaml_differential_test.${TARGET_NAME_LIB}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/process/include)
target_compile_definitions(orch_yaml_differential_test.${TARGET_NAME_LIB} PRIVATE
    PRAKTOR_ORCH_CORPUS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")

if(PRAKTOR_ENABLE_C11_ACTIONS)
    target_link_libraries(workflow_owner_differential_test.${TARGET_NAME_LIB}
        PRIVATE praktor_c11_orch_session_plan
                praktor_workflow_snapshot
                praktor_workflow_dag_state
                praktor_workflow_task_registry
                praktor_c11_workflow_snapshot_values
                praktor_c11_workflow_parallel_process_guard
                praktor_c11_native_host Salts::CFlow)
    set_tests_properties(workflow_owner_differential_test.${TARGET_NAME_LIB}
        PROPERTIES TIMEOUT 45)
endif()

target_include_directories(download_executor_test.${TARGET_NAME_LIB}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_include_directories(workflow_execution_regression_test.${TARGET_NAME_LIB}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(workflow_execution_regression_test.${TARGET_NAME_LIB}
    PROPERTIES TIMEOUT 60)

# The opt-in C11 test suite exercises native process cancellation. Keep a
# hard CTest timeout so a shutdown fault is reported as a failed qualification,
# not an unbounded CI job; this is not a substitute for a lease-safe API.
if(PRAKTOR_ENABLE_C11_ACTIONS AND NOT ENABLE_SCRIPT_ENGINE)
    set_tests_properties(core_only_workflow_test.${TARGET_NAME_LIB}
        PROPERTIES TIMEOUT 40)
endif()

if(ENABLE_SCRIPT_ENGINE)
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
endif()

if(WIN32)
    add_executable(managed_process_fixture helpers/managed_process_fixture.c)
    add_executable(managed_process_fixture_alt helpers/managed_process_fixture.c)
    set_target_properties(managed_process_fixture managed_process_fixture_alt PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
    set_target_properties(managed_process_fixture_alt PROPERTIES
        OUTPUT_NAME "managed_process_fixture"
        SUFFIX ".com"
    )
    if(MSVC)
        set_target_properties(managed_process_fixture_alt PROPERTIES
            PDB_NAME "managed_process_fixture_alt"
        )
        target_link_options(managed_process_fixture_alt PRIVATE
            "/ILK:$<TARGET_FILE_DIR:managed_process_fixture_alt>/managed_process_fixture_alt.ilk"
        )
    endif()
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
    target_compile_definitions(
        managed_process_executor_test.${TARGET_NAME_LIB}
        PRIVATE PRAKTOR_MANAGED_PROCESS_FIXTURE="$<TARGET_FILE:managed_process_fixture>"
                PRAKTOR_MANAGED_PROCESS_FIXTURE_ALT="$<TARGET_FILE:managed_process_fixture_alt>"
    )
    if(TARGET praktor_process_native_test)
        add_dependencies(praktor_process_native_test managed_process_fixture)
        target_compile_definitions(praktor_process_native_test PRIVATE
            PRAKTOR_NATIVE_FIXTURE="$<TARGET_FILE:managed_process_fixture>")
    endif()
    if(TARGET workflow_runner_test.${TARGET_NAME_LIB})
        add_dependencies(workflow_runner_test.${TARGET_NAME_LIB} managed_process_fixture)
        target_compile_definitions(
            workflow_runner_test.${TARGET_NAME_LIB}
            PRIVATE PRAKTOR_MANAGED_PROCESS_FIXTURE="$<TARGET_FILE:managed_process_fixture>"
                    PRAKTOR_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        set_tests_properties(workflow_runner_test.${TARGET_NAME_LIB}
            PROPERTIES RESOURCE_LOCK praktor_managed_process_fixture)
    endif()
    set_tests_properties(
        managed_process_executor_test.${TARGET_NAME_LIB}
        PROPERTIES RESOURCE_LOCK praktor_managed_process_fixture
    )
endif()

if(PRAKTOR_ENABLE_C11_ACTIONS)
    add_executable(actions_c11_session_yaml_differential_test
        ${CMAKE_CURRENT_SOURCE_DIR}/../../actions/c11/session_yaml_differential_test.cpp)
    target_link_libraries(actions_c11_session_yaml_differential_test PRIVATE
        ${TARGET_NAME_LIB} actions praktor_c11_session praktor_c11_native_host praktor_test_support)
    add_test(NAME actions_c11_session_yaml_differential_test
        COMMAND actions_c11_session_yaml_differential_test)
    set_tests_properties(actions_c11_session_yaml_differential_test PROPERTIES TIMEOUT 30)
endif()
