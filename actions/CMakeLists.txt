cmake_minimum_required(VERSION 3.20)
# C11 canonical Actions and Salts::Process only: no C++ fallback.
include(${CMAKE_CURRENT_SOURCE_DIR}/output_targets.cmake)
if(NOT TARGET Salts::BehaviorTree OR NOT TARGET Salts::Process)
    message(FATAL_ERROR "SaltsUtils BehaviorTree/Process C11 providers are required")
endif()
if(TRUE)
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
            praktor_c11_workflow_parallel_process_guard Salts::CFlow
            Salts::Platform Salts::TinyTest)
        set_target_properties(actions_c11_workflow_parallel_worker_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_workflow_parallel_worker_test
            COMMAND actions_c11_workflow_parallel_worker_test)

        # Qualification: owner-only DAG readiness drives the canonical CFlow
        # Manual/WorkerExecutor. No second queue, new worker or production
        # WorkflowExecutor substitution.
        add_executable(actions_c11_workflow_dag_cflow_test
            c11/workflow_dag_cflow_test.c)
        target_link_libraries(actions_c11_workflow_dag_cflow_test PRIVATE
            praktor_workflow_dag_state Salts::CFlow Salts::Platform Salts::TinyTest)
        set_target_properties(actions_c11_workflow_dag_cflow_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_workflow_dag_cflow_test
            COMMAND actions_c11_workflow_dag_cflow_test)
        set_tests_properties(actions_c11_workflow_dag_cflow_test
            PROPERTIES TIMEOUT 30)

        # Real YAML Snapshot / DAG readiness, CFlow WorkerExecutor, root
        # variables, typed TaskRegistry and native Process lease barrier.
        # Deterministic callback effects only; no production leaf cutover.
        add_executable(actions_c11_workflow_dag_publication_test
            c11/workflow_dag_publication_test.c)
        target_link_libraries(actions_c11_workflow_dag_publication_test PRIVATE
            praktor_workflow_dag_state
            praktor_c11_workflow_snapshot_values
            praktor_c11_workflow_parallel_process_guard
            Salts::CFlow Salts::TinyTest)
        set_target_properties(actions_c11_workflow_dag_publication_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_workflow_dag_publication_test
            COMMAND actions_c11_workflow_dag_publication_test)
        set_tests_properties(actions_c11_workflow_dag_publication_test
            PROPERTIES TIMEOUT 30)
    endif()

    # Strict Process-retainer barrier for cross-branch commit; this wrapper
    # borrows the existing SaltsUtils Process recovery owner and never starts
    # a Process or invents completion when a lease is still active.
    add_library(praktor_c11_workflow_parallel_process_guard STATIC
        c11/workflow_parallel_process_guard.c
        c11/workflow_parallel_process_guard.h
        c11/workflow_owner_settlement.c
        c11/workflow_owner_settlement.h)
    target_include_directories(praktor_c11_workflow_parallel_process_guard PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/c11>)
    target_link_libraries(praktor_c11_workflow_parallel_process_guard PUBLIC
        praktor_c11_workflow_parallel_commit praktor_workflow_dag_state
        Salts::Process Salts::CFlow)
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
    include(${CMAKE_CURRENT_SOURCE_DIR}/c11/session_core_targets.cmake)

    # Source-only native conversion of canonical Orch AST to C11 Session plan.
    # No C++ bridge, alternate parser, scheduler or implicit Legacy fallback.
    add_library(praktor_c11_orch_session_plan STATIC
        c11/orch_session_plan.c c11/orch_session_plan.h)
    target_include_directories(praktor_c11_orch_session_plan PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/c11>)
    target_link_libraries(praktor_c11_orch_session_plan PUBLIC
        praktor_c11_session_core praktor_orch_tree)
    target_compile_features(praktor_c11_orch_session_plan PUBLIC c_std_11)
    set_target_properties(praktor_c11_orch_session_plan PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON FOLDER "actions/c11")

    if(ENABLE_TESTS)
        add_executable(actions_c11_orch_session_plan_test
            c11/orch_session_plan_test.c)
        target_link_libraries(actions_c11_orch_session_plan_test PRIVATE
            praktor_c11_orch_session_plan
            praktor_workflow_snapshot
            praktor_c11_native_host
            Salts::TinyTest)
        set_target_properties(actions_c11_orch_session_plan_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_orch_session_plan_test
            COMMAND actions_c11_orch_session_plan_test)
        set_tests_properties(actions_c11_orch_session_plan_test
            PROPERTIES TIMEOUT 30)

        # Complete C11 YAML -> Orch -> Session -> owner-driven ManualExecutor
        # DAG qualification. No worker-thread Session transfer or Legacy path.
        add_executable(actions_c11_orch_dag_native_session_test
            c11/orch_dag_native_session_test.c)
        target_link_libraries(actions_c11_orch_dag_native_session_test PRIVATE
            praktor_c11_orch_session_plan
            praktor_workflow_snapshot
            praktor_workflow_dag_state
            praktor_workflow_task_registry
            praktor_c11_workflow_snapshot_values
            praktor_c11_workflow_parallel_process_guard
            praktor_c11_native_host
            Salts::CFlow Salts::TinyTest)
        set_target_properties(actions_c11_orch_dag_native_session_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_orch_dag_native_session_test
            COMMAND actions_c11_orch_dag_native_session_test)
        set_tests_properties(actions_c11_orch_dag_native_session_test
            PROPERTIES TIMEOUT 30)

        # WorkerExecutor owns the Session ONLY for one run callback; it
        # returns bounded immutable results to the exclusive DAG Owner.
        # This does not enable Shell/Process Session migration or production.
        add_executable(actions_c11_orch_worker_session_test
            c11/orch_worker_session_test.c)
        target_link_libraries(actions_c11_orch_worker_session_test PRIVATE
            praktor_c11_orch_session_plan
            praktor_workflow_snapshot praktor_workflow_dag_state
            praktor_workflow_task_registry
            praktor_c11_workflow_snapshot_values
            praktor_c11_workflow_parallel_process_guard
            praktor_c11_native_host
            Salts::CFlow Salts::TinyTest)
        set_target_properties(actions_c11_orch_worker_session_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_orch_worker_session_test
            COMMAND actions_c11_orch_worker_session_test)
        set_tests_properties(actions_c11_orch_worker_session_test
            PROPERTIES TIMEOUT 45)

        # Real Shell/NativeIO in a worker callback, with the same retainer
        # transferred to the application Owner ONLY after CFlow finalization.
        # This is a test-only lifecycle, not a production scheduler switch.
        add_executable(actions_c11_orch_worker_process_test
            c11/orch_worker_process_test.c)
        target_link_libraries(actions_c11_orch_worker_process_test PRIVATE
            praktor_c11_orch_session_plan
            praktor_workflow_snapshot praktor_workflow_dag_state
            praktor_workflow_task_registry
            praktor_c11_workflow_snapshot_values
            praktor_c11_workflow_parallel_process_guard
            praktor_c11_native_host
            Salts::CFlow Salts::Platform Salts::TinyTest)
        set_target_properties(actions_c11_orch_worker_process_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_orch_worker_process_test
            COMMAND actions_c11_orch_worker_process_test)
        set_tests_properties(actions_c11_orch_worker_process_test
            PROPERTIES TIMEOUT 45)

        # Real canonical flat Shell → C11 Session → Salts::Process/NativeIO →
        # retainer → DAG Owner, opt-in and never a production fallback.
        add_executable(actions_c11_orch_dag_shell_process_test
            c11/orch_dag_shell_process_test.c)
        target_link_libraries(actions_c11_orch_dag_shell_process_test PRIVATE
            praktor_c11_orch_session_plan
            praktor_workflow_snapshot praktor_workflow_dag_state
            praktor_workflow_task_registry
            praktor_c11_workflow_snapshot_values
            praktor_c11_workflow_parallel_process_guard
            praktor_c11_native_host
            Salts::CFlow Salts::Platform Salts::TinyTest)
        set_target_properties(actions_c11_orch_dag_shell_process_test PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            LINKER_LANGUAGE C FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_orch_dag_shell_process_test
            COMMAND actions_c11_orch_dag_shell_process_test)
        set_tests_properties(actions_c11_orch_dag_shell_process_test
            PROPERTIES TIMEOUT 45)
    endif()

endif()
if(ENABLE_TESTS)
    foreach(test IN ITEMS session_c_test native_host_test value_store_test
            regex_tre_test tree_adapter_test plan_test)
        if(test STREQUAL "session_c_test")
            set(_deps praktor_c11_session_core praktor_c11_native_host)
        elseif(test STREQUAL "native_host_test")
            set(_deps praktor_c11_native_host)
        elseif(test STREQUAL "value_store_test")
            set(_deps praktor_c11_value_store)
        elseif(test STREQUAL "regex_tre_test")
            set(_deps praktor_regex_tre)
        elseif(test STREQUAL "tree_adapter_test")
            set(_deps praktor_c11_bt_core)
        else()
            set(_deps praktor_c11_session_core)
        endif()
        add_executable(actions_c11_${test} c11/${test}.c)
        target_link_libraries(actions_c11_${test} PRIVATE ${_deps} Salts::TinyTest)
        set_target_properties(actions_c11_${test} PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
        add_test(NAME actions_c11_${test} COMMAND actions_c11_${test})
    endforeach()
endif()
