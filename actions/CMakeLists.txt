cmake_minimum_required(VERSION 3.16)

project(
    actions
    VERSION 2.0.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PRAKTOR_ENABLE_C11_ACTIONS
       "Build the experimental Salts C11 action-tree adapter and its tests"
       OFF)

cmake_add_source(ALL_SOURCE_FILES RECURSE
    DIRS src core
    PATTERNS "*.cpp"
)
cmake_add_source(ALL_HEADER_FILES RECURSE
    DIRS include/actions core
    PATTERNS "*.hpp" "*.h"
)
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "praktor_integration\\.cpp$")
list(FILTER ALL_HEADER_FILES EXCLUDE REGEX "praktor_integration\\.hpp$")

# ── MAIN LIBRARY ──

add_library(actions ${ALL_SOURCE_FILES} ${ALL_HEADER_FILES})

target_include_directories(
    actions
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
           $<INSTALL_INTERFACE:include>
)

target_compile_features(actions PUBLIC cxx_std_17)
target_compile_definitions(actions PRIVATE $<$<BOOL:WIN32>:NOMINMAX>)
set_target_properties(actions PROPERTIES POSITION_INDEPENDENT_CODE ON)
cmake_config_target(actions NO_INSTALL FOLDER "actions/" VERSION ${PROJECT_VERSION})

# ── DEPENDENCIES ──

include(${CMAKE_CURRENT_SOURCE_DIR}/output_targets.cmake)
target_link_libraries(actions PUBLIC praktor_shell PRIVATE Salts::JsonParser praktor_output_core)

if(WIN32)
    target_link_libraries(actions PRIVATE psapi)
endif()

# This target is deliberately opt-in until the Praktor parity corpus passes.
# Enabling it is fail-fast: it never silently falls back when the selected
# SaltsUtils SDK does not export Salts::BehaviorTree.
if(PRAKTOR_ENABLE_C11_ACTIONS)
    if(NOT TARGET Salts::BehaviorTree)
        message(FATAL_ERROR
            "PRAKTOR_ENABLE_C11_ACTIONS requires an installed SaltsUtils SDK exporting Salts::BehaviorTree")
    endif()
    if(NOT TARGET Salts::Process)
        message(FATAL_ERROR
            "PRAKTOR_ENABLE_C11_ACTIONS requires Salts::Process; Shell execution never falls back to the legacy ShellExecutor")
    endif()

    # Native C11 adapter: no C++ actions::Executor or Shell dependency.
    add_library(praktor_c11_bt_core STATIC
        c11/tree_adapter.c
        c11/tree_adapter.h
    )
    target_include_directories(praktor_c11_bt_core
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/c11>
    )
    target_link_libraries(praktor_c11_bt_core PUBLIC Salts::BehaviorTree)
    target_compile_features(praktor_c11_bt_core PUBLIC c_std_11)
    set_target_properties(praktor_c11_bt_core PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
        FOLDER "actions/c11"
    )
    cmake_config_target(praktor_c11_bt_core NO_INSTALL NO_VERSION FOLDER "actions/c11")

    # Independent bounded C11 key/value storage; no BehaviorTree or actions dependency.
    add_library(praktor_c11_value_store STATIC c11/value_store.c c11/value_store.h)
    target_include_directories(praktor_c11_value_store PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/c11>)
    target_compile_features(praktor_c11_value_store PUBLIC c_std_11)
    set_target_properties(praktor_c11_value_store PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON FOLDER "actions/c11")
    cmake_config_target(praktor_c11_value_store NO_INSTALL NO_VERSION FOLDER "actions/c11")

    # Narrow C WorkflowContext admission: overlay root string variables via the
    # canonical C11 value store's atomic set_many contract. This is not a
    # replacement for structured WorkflowValue, TaskRegistry or task scopes.
    add_library(praktor_c11_workflow_snapshot_values STATIC
        c11/workflow_snapshot_values.c c11/workflow_snapshot_values.h)
    target_include_directories(praktor_c11_workflow_snapshot_values PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/c11>)
    target_link_libraries(praktor_c11_workflow_snapshot_values PUBLIC
        praktor_workflow_snapshot praktor_c11_value_store)
    target_compile_features(praktor_c11_workflow_snapshot_values PUBLIC c_std_11)
    set_target_properties(praktor_c11_workflow_snapshot_values PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON FOLDER "actions/c11")

    if(ENABLE_TESTS)
        add_executable(actions_c11_workflow_snapshot_values_test
            c11/workflow_snapshot_values_test.c)
        target_link_libraries(actions_c11_workflow_snapshot_values_test PRIVATE
            praktor_c11_workflow_snapshot_values praktor_c11_native_host Salts::TinyTest)
        set_target_properties(actions_c11_workflow_snapshot_values_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_workflow_snapshot_values_test
            COMMAND actions_c11_workflow_snapshot_values_test)

        # Compiles the same C11 handoff with test-only failed allocation,
        # clone and atomic batch publication boundaries; no C++ executor.
        add_executable(actions_c11_workflow_snapshot_values_fault_test
            c11/workflow_snapshot_values_fault_test.c)
        target_link_libraries(actions_c11_workflow_snapshot_values_fault_test PRIVATE
            praktor_workflow_snapshot praktor_c11_value_store Salts::TinyTest)
        set_target_properties(actions_c11_workflow_snapshot_values_fault_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_workflow_snapshot_values_fault_test
            COMMAND actions_c11_workflow_snapshot_values_fault_test)
    endif()

    # Qualification-only cross-branch publication: canonical C11 optimistic
    # value txn group + the C-owned TaskRegistry staged JSON owner.
    # No C++ WorkflowContext, scheduler or second transaction implementation.
    add_library(praktor_c11_workflow_parallel_commit STATIC
        c11/workflow_parallel_commit.c c11/workflow_parallel_commit.h)
    target_include_directories(praktor_c11_workflow_parallel_commit PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/c11>)
    target_link_libraries(praktor_c11_workflow_parallel_commit PUBLIC
        praktor_c11_value_store praktor_workflow_task_registry)
    target_compile_features(praktor_c11_workflow_parallel_commit PUBLIC c_std_11)
    set_target_properties(praktor_c11_workflow_parallel_commit PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON FOLDER "actions/c11")

    if(ENABLE_TESTS)
        add_executable(actions_c11_workflow_parallel_commit_test
            c11/workflow_parallel_commit_test.c)
        target_link_libraries(actions_c11_workflow_parallel_commit_test PRIVATE
            praktor_c11_workflow_parallel_commit Salts::TinyTest)
        set_target_properties(actions_c11_workflow_parallel_commit_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_workflow_parallel_commit_test
            COMMAND actions_c11_workflow_parallel_commit_test)

        # Test-only interposition of value_txn_commit_group: the real typed
        # owner is staged before the failed root group and never published.
        add_executable(actions_c11_workflow_parallel_commit_fault_test
            c11/workflow_parallel_commit_fault_test.c)
        target_link_libraries(actions_c11_workflow_parallel_commit_fault_test PRIVATE
            praktor_workflow_task_registry praktor_c11_value_store Salts::TinyTest)
        set_target_properties(actions_c11_workflow_parallel_commit_fault_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_workflow_parallel_commit_fault_test
            COMMAND actions_c11_workflow_parallel_commit_fault_test)

        # Real Salts CFlow ManualExecutor is the ONLY owner of the task queue.
        # This qualification verifies callback settlement before publication;
        # the production scheduler and worker cancellation remain untouched.
        add_executable(actions_c11_workflow_parallel_executor_test
            c11/workflow_parallel_executor_test.c)
        target_link_libraries(actions_c11_workflow_parallel_executor_test PRIVATE
            praktor_c11_workflow_parallel_commit Salts::CFlow Salts::TinyTest)
        set_target_properties(actions_c11_workflow_parallel_executor_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_workflow_parallel_executor_test
            COMMAND actions_c11_workflow_parallel_executor_test)

        # Real concurrent Salts CFlow WorkerExecutor; no Praktor pool, and
        # only the caller/owner publishes committed typed branch outputs.
        add_executable(actions_c11_workflow_parallel_worker_test
            c11/workflow_parallel_worker_test.c)
        target_link_libraries(actions_c11_workflow_parallel_worker_test PRIVATE
            praktor_c11_workflow_parallel_commit Salts::CFlow Salts::Platform
            Salts::TinyTest)
        set_target_properties(actions_c11_workflow_parallel_worker_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_workflow_parallel_worker_test
            COMMAND actions_c11_workflow_parallel_worker_test)
    endif()

    # Strict Process-retainer barrier for cross-branch commit; this wrapper
    # borrows the existing SaltsUtils Process recovery owner and never starts
    # a Process or invents completion when a lease is still active.
    add_library(praktor_c11_workflow_parallel_process_guard STATIC
        c11/workflow_parallel_process_guard.c
        c11/workflow_parallel_process_guard.h)
    target_include_directories(praktor_c11_workflow_parallel_process_guard PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/c11>)
    target_link_libraries(praktor_c11_workflow_parallel_process_guard PUBLIC
        praktor_c11_workflow_parallel_commit Salts::Process Salts::CFlow)
    target_compile_features(praktor_c11_workflow_parallel_process_guard PUBLIC c_std_11)
    set_target_properties(praktor_c11_workflow_parallel_process_guard PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON FOLDER "actions/c11")

    if(ENABLE_TESTS)
        add_executable(actions_c11_workflow_parallel_process_guard_test
            c11/workflow_parallel_process_guard_test.c)
        target_link_libraries(actions_c11_workflow_parallel_process_guard_test PRIVATE
            praktor_c11_workflow_parallel_process_guard Salts::TinyTest)
        set_target_properties(actions_c11_workflow_parallel_process_guard_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_workflow_parallel_process_guard_test
            COMMAND actions_c11_workflow_parallel_process_guard_test)

        # Only the Process recovery observer is faulted; real transactions and
        # typed-output owners still use their canonical C11 implementations.
        add_executable(actions_c11_workflow_parallel_process_guard_fault_test
            c11/workflow_parallel_process_guard_fault_test.c)
        target_link_libraries(actions_c11_workflow_parallel_process_guard_fault_test PRIVATE
            praktor_c11_workflow_parallel_commit Salts::Process
            Salts::CFlow Salts::TinyTest)
        set_target_properties(actions_c11_workflow_parallel_process_guard_fault_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_workflow_parallel_process_guard_fault_test
            COMMAND actions_c11_workflow_parallel_process_guard_fault_test)

        # Real shell child / native Process lease handoff through the one
        # existing C11 driver + SaltsUtils recovery owner. Never force-free
        # a live NativeIO completion payload to make the test succeed.
        add_executable(actions_c11_workflow_parallel_process_driver_test
            c11/workflow_parallel_process_driver_test.c)
        target_link_libraries(actions_c11_workflow_parallel_process_driver_test PRIVATE
            praktor_c11_workflow_parallel_process_guard
            praktor_process_driver Salts::Platform Salts::TinyTest)
        set_target_properties(actions_c11_workflow_parallel_process_driver_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_workflow_parallel_process_driver_test
            COMMAND actions_c11_workflow_parallel_process_driver_test)
        set_tests_properties(actions_c11_workflow_parallel_process_driver_test
            PROPERTIES TIMEOUT 25)
    endif()

    # Independent AST/Process Session; never links actions or praktor_shell.
    include(${CMAKE_CURRENT_SOURCE_DIR}/c11/session_targets.cmake)

    # Explicit legacy compatibility constructors and Blackboard overloads only.
    add_library(praktor_actions_c11 STATIC c11/session_legacy_adapter.cpp)
    target_include_directories(praktor_actions_c11 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(praktor_actions_c11 PUBLIC praktor_c11_session actions)
    target_compile_features(praktor_actions_c11 PRIVATE cxx_std_17)
    set_target_properties(praktor_actions_c11 PROPERTIES POSITION_INDEPENDENT_CODE ON)
    cmake_config_target(praktor_actions_c11 NO_INSTALL NO_VERSION FOLDER "actions/c11")

    if(ENABLE_TESTS)
        add_executable(actions_c11_session_c_test c11/session_c_test.c)
        target_link_libraries(actions_c11_session_c_test PRIVATE praktor_c11_session_core praktor_c11_native_host Salts::TinyTest)
        add_test(NAME actions_c11_session_c_test COMMAND actions_c11_session_c_test)
        set_tests_properties(actions_c11_session_c_test PROPERTIES TIMEOUT 30)
        add_executable(actions_c11_native_host_test c11/native_host_test.c)
        target_link_libraries(actions_c11_native_host_test PRIVATE praktor_c11_native_host Salts::TinyTest)
        add_test(NAME actions_c11_native_host_test COMMAND actions_c11_native_host_test)
        add_executable(actions_c11_regex_tre_test c11/regex_tre_test.c)
        target_link_libraries(actions_c11_regex_tre_test PRIVATE praktor_regex_tre Salts::TinyTest)
        add_test(NAME actions_c11_regex_tre_test COMMAND actions_c11_regex_tre_test)
        add_executable(actions_c11_tree_adapter_test c11/tree_adapter_test.c)
        target_link_libraries(actions_c11_tree_adapter_test PRIVATE praktor_c11_bt_core)
        target_compile_features(actions_c11_tree_adapter_test PRIVATE c_std_11)
        set_target_properties(actions_c11_tree_adapter_test PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
            FOLDER "actions/c11/tests"
        )
        add_test(NAME actions_c11_tree_adapter_test COMMAND actions_c11_tree_adapter_test)
        add_executable(actions_c11_value_store_test c11/value_store_test.c)
        target_link_libraries(actions_c11_value_store_test PRIVATE praktor_c11_value_store Salts::TinyTest)
        target_compile_features(actions_c11_value_store_test PRIVATE c_std_11)
        set_target_properties(actions_c11_value_store_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_value_store_test COMMAND actions_c11_value_store_test)

        # Header contract: compile the C11 session API with Salts/AST
        # model headers only, without linking or including the legacy Executor.
        add_library(actions_c11_session_header_contract OBJECT
            c11/session_header_contract.cpp)
        target_include_directories(actions_c11_session_header_contract PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(actions_c11_session_header_contract PRIVATE
            praktor_c11_bt_core Salts::Process)
        target_compile_features(actions_c11_session_header_contract PRIVATE cxx_std_17)
        set_target_properties(actions_c11_session_header_contract PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
            FOLDER "actions/c11/tests"
        )

        add_executable(actions_c11_session_independent_test c11/session_independent_test.cpp)
        target_link_libraries(actions_c11_session_independent_test PRIVATE praktor_c11_session praktor_c11_native_host praktor_test_support)
        add_test(NAME actions_c11_session_independent_test COMMAND actions_c11_session_independent_test)
        set_tests_properties(actions_c11_session_independent_test PROPERTIES TIMEOUT 30)

        add_executable(actions_c11_tree_plan_test c11/plan_test.c)
        target_link_libraries(actions_c11_tree_plan_test PRIVATE praktor_c11_session_core Salts::TinyTest)
        target_compile_features(actions_c11_tree_plan_test PRIVATE c_std_11)
        set_target_properties(actions_c11_tree_plan_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_tree_plan_test COMMAND actions_c11_tree_plan_test)

        add_executable(actions_c11_tree_executor_session_test c11/tree_executor_session_test.cpp)
        target_link_libraries(actions_c11_tree_executor_session_test PRIVATE praktor_actions_c11 praktor_c11_value_store praktor_c11_native_host)
        target_compile_features(actions_c11_tree_executor_session_test PRIVATE cxx_std_17)
        set_target_properties(actions_c11_tree_executor_session_test PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
            FOLDER "actions/c11/tests"
        )
        add_test(NAME actions_c11_tree_executor_session_test COMMAND actions_c11_tree_executor_session_test)
    endif()
endif()

# ── EXAMPLES ──

add_subdirectory(examples)

# ── TESTS ──

if(ENABLE_TESTS)
    add_subdirectory(test)
endif()
