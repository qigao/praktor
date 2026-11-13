add_executable(actions_c11_workflow_snapshot_values_test
    workflow_snapshot_values_test.c)
target_link_libraries(actions_c11_workflow_snapshot_values_test PRIVATE
    praktor_c11_workflow_snapshot_values praktor_c11_native_host Salts::TinyTest)
set_target_properties(actions_c11_workflow_snapshot_values_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "actions/tests")
add_test(NAME actions_c11_workflow_snapshot_values_test
    COMMAND actions_c11_workflow_snapshot_values_test)

# Compiles the same C11 handoff with test-only failed allocation,
# clone and atomic batch publication boundaries; no C++ executor.
add_executable(actions_c11_workflow_snapshot_values_fault_test
    workflow_snapshot_values_fault_test.c)
target_link_libraries(actions_c11_workflow_snapshot_values_fault_test PRIVATE
    praktor_workflow_snapshot praktor_c11_value_store Salts::TinyTest)
set_target_properties(actions_c11_workflow_snapshot_values_fault_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "actions/tests")
add_test(NAME actions_c11_workflow_snapshot_values_fault_test
    COMMAND actions_c11_workflow_snapshot_values_fault_test)

add_executable(actions_c11_workflow_parallel_commit_test
    workflow_parallel_commit_test.c)
target_link_libraries(actions_c11_workflow_parallel_commit_test PRIVATE
    praktor_c11_workflow_parallel_commit Salts::TinyTest)
set_target_properties(actions_c11_workflow_parallel_commit_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "actions/tests")
add_test(NAME actions_c11_workflow_parallel_commit_test
    COMMAND actions_c11_workflow_parallel_commit_test)

# Test-only interposition of value_txn_commit_group: the real typed
# owner is staged before the failed root group and never published.
add_executable(actions_c11_workflow_parallel_commit_fault_test
    workflow_parallel_commit_fault_test.c)
target_link_libraries(actions_c11_workflow_parallel_commit_fault_test PRIVATE
    praktor_workflow_task_registry praktor_c11_value_store Salts::TinyTest)
set_target_properties(actions_c11_workflow_parallel_commit_fault_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "actions/tests")
add_test(NAME actions_c11_workflow_parallel_commit_fault_test
    COMMAND actions_c11_workflow_parallel_commit_fault_test)

# Real Salts CFlow ManualExecutor is the ONLY owner of the task queue.
# This qualification verifies callback settlement before publication;
# the production scheduler and worker cancellation remain untouched.
add_executable(actions_c11_workflow_parallel_executor_test
    workflow_parallel_executor_test.c)
target_link_libraries(actions_c11_workflow_parallel_executor_test PRIVATE
    praktor_c11_workflow_parallel_commit CMeta::CFlow Salts::TinyTest)
set_target_properties(actions_c11_workflow_parallel_executor_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "actions/tests")
add_test(NAME actions_c11_workflow_parallel_executor_test
    COMMAND actions_c11_workflow_parallel_executor_test)

# Real concurrent Salts CFlow WorkerExecutor; no Praktor pool, and
# only the caller/owner publishes committed typed branch outputs.
add_executable(actions_c11_workflow_parallel_worker_test
    workflow_parallel_worker_test.c)
target_link_libraries(actions_c11_workflow_parallel_worker_test PRIVATE
    praktor_c11_workflow_parallel_process_guard CMeta::CFlow
    Salts::Core Salts::TinyTest)
set_target_properties(actions_c11_workflow_parallel_worker_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "actions/tests")
add_test(NAME actions_c11_workflow_parallel_worker_test
    COMMAND actions_c11_workflow_parallel_worker_test)

# Owner-only DAG readiness drives the canonical CFlow Manual/WorkerExecutor.
add_executable(actions_c11_workflow_dag_cflow_test
    workflow_dag_cflow_test.c)
target_link_libraries(actions_c11_workflow_dag_cflow_test PRIVATE
    praktor_workflow_dag_state CMeta::CFlow Salts::Core Salts::TinyTest)
set_target_properties(actions_c11_workflow_dag_cflow_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "actions/tests")
add_test(NAME actions_c11_workflow_dag_cflow_test
    COMMAND actions_c11_workflow_dag_cflow_test)
set_tests_properties(actions_c11_workflow_dag_cflow_test
    PROPERTIES TIMEOUT 30)

# Real YAML Snapshot / DAG readiness, CFlow WorkerExecutor, root
# variables, typed TaskRegistry and native Process lease barrier.
# Deterministic callback effects only; no production leaf cutover.
add_executable(actions_c11_workflow_dag_publication_test
    workflow_dag_publication_test.c)
target_link_libraries(actions_c11_workflow_dag_publication_test PRIVATE
    praktor_workflow_dag_state
    praktor_c11_workflow_snapshot_values
    praktor_c11_workflow_parallel_process_guard
    CMeta::CFlow Salts::TinyTest)
set_target_properties(actions_c11_workflow_dag_publication_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "actions/tests")
add_test(NAME actions_c11_workflow_dag_publication_test
    COMMAND actions_c11_workflow_dag_publication_test)
set_tests_properties(actions_c11_workflow_dag_publication_test
    PROPERTIES TIMEOUT 30)

add_executable(actions_c11_workflow_parallel_process_guard_test
    workflow_parallel_process_guard_test.c)
target_link_libraries(actions_c11_workflow_parallel_process_guard_test PRIVATE
    praktor_c11_workflow_parallel_process_guard Salts::TinyTest)
set_target_properties(actions_c11_workflow_parallel_process_guard_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "actions/tests")
add_test(NAME actions_c11_workflow_parallel_process_guard_test
    COMMAND actions_c11_workflow_parallel_process_guard_test)

# Only the Process recovery observer is faulted; real transactions and
# typed-output owners still use their canonical C11 implementations.
add_executable(actions_c11_workflow_parallel_process_guard_fault_test
    workflow_parallel_process_guard_fault_test.c)
target_link_libraries(actions_c11_workflow_parallel_process_guard_fault_test PRIVATE
    praktor_c11_workflow_parallel_commit praktor_process_retainer
    CMeta::CFlow Salts::TinyTest)
set_target_properties(actions_c11_workflow_parallel_process_guard_fault_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "actions/tests")
add_test(NAME actions_c11_workflow_parallel_process_guard_fault_test
    COMMAND actions_c11_workflow_parallel_process_guard_fault_test)

# Real shell child / native Process lease handoff through the one
# existing C11 driver + Praktor recovery owner. Never force-free
# a live NativeIO completion payload to make the test succeed.
add_executable(actions_c11_workflow_parallel_process_driver_test
    workflow_parallel_process_driver_test.c)
target_link_libraries(actions_c11_workflow_parallel_process_driver_test PRIVATE
    praktor_c11_workflow_parallel_process_guard
    praktor_process_driver Salts::Core Salts::TinyTest)
set_target_properties(actions_c11_workflow_parallel_process_driver_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "actions/tests")
add_test(NAME actions_c11_workflow_parallel_process_driver_test
    COMMAND actions_c11_workflow_parallel_process_driver_test)
set_tests_properties(actions_c11_workflow_parallel_process_driver_test
    PROPERTIES TIMEOUT 25)

add_executable(actions_c11_orch_session_plan_test
    orch_session_plan_test.c)
target_link_libraries(actions_c11_orch_session_plan_test PRIVATE
    praktor_c11_orch_session_plan
    praktor_workflow_snapshot
    praktor_c11_native_host
    Salts::TinyTest)
set_target_properties(actions_c11_orch_session_plan_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "actions/tests")
add_test(NAME actions_c11_orch_session_plan_test
    COMMAND actions_c11_orch_session_plan_test)
set_tests_properties(actions_c11_orch_session_plan_test
    PROPERTIES TIMEOUT 30)

# Complete C11 YAML -> Orch -> Session -> owner-driven ManualExecutor
# DAG qualification. No worker-thread Session transfer or Legacy path.
add_executable(actions_c11_orch_dag_native_session_test
    orch_dag_native_session_test.c)
target_link_libraries(actions_c11_orch_dag_native_session_test PRIVATE
    praktor_c11_orch_session_plan
    praktor_workflow_snapshot
    praktor_workflow_dag_state
    praktor_workflow_task_registry
    praktor_c11_workflow_snapshot_values
    praktor_c11_workflow_parallel_process_guard
    praktor_c11_native_host
    CMeta::CFlow Salts::TinyTest)
set_target_properties(actions_c11_orch_dag_native_session_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "actions/tests")
add_test(NAME actions_c11_orch_dag_native_session_test
    COMMAND actions_c11_orch_dag_native_session_test)
set_tests_properties(actions_c11_orch_dag_native_session_test
    PROPERTIES TIMEOUT 30)

# WorkerExecutor owns the Session ONLY for one run callback; it
# returns bounded immutable results to the exclusive DAG Owner.
# This does not enable Shell/Process Session migration or production.
add_executable(actions_c11_orch_worker_session_test
    orch_worker_session_test.c)
target_link_libraries(actions_c11_orch_worker_session_test PRIVATE
    praktor_c11_orch_session_plan
    praktor_workflow_snapshot praktor_workflow_dag_state
    praktor_workflow_task_registry
    praktor_c11_workflow_snapshot_values
    praktor_c11_workflow_parallel_process_guard
    praktor_c11_native_host
    CMeta::CFlow Salts::TinyTest)
set_target_properties(actions_c11_orch_worker_session_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "actions/tests")
add_test(NAME actions_c11_orch_worker_session_test
    COMMAND actions_c11_orch_worker_session_test)
set_tests_properties(actions_c11_orch_worker_session_test
    PROPERTIES TIMEOUT 45)

# Real Shell/NativeIO in a worker callback, with the same retainer
# transferred to the application Owner ONLY after CFlow finalization.
# This is a test-only lifecycle, not a production scheduler switch.
add_executable(actions_c11_orch_worker_process_test
    orch_worker_process_test.c)
target_link_libraries(actions_c11_orch_worker_process_test PRIVATE
    praktor_c11_orch_session_plan
    praktor_workflow_snapshot praktor_workflow_dag_state
    praktor_workflow_task_registry
    praktor_c11_workflow_snapshot_values
    praktor_c11_workflow_parallel_process_guard
    praktor_c11_native_host
    CMeta::CFlow Salts::Core Salts::TinyTest)
set_target_properties(actions_c11_orch_worker_process_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "actions/tests")
add_test(NAME actions_c11_orch_worker_process_test
    COMMAND actions_c11_orch_worker_process_test)
set_tests_properties(actions_c11_orch_worker_process_test
    PROPERTIES TIMEOUT 45)

# Real canonical flat Shell → C11 Session → SaltsBase NativeIO →
# retainer → DAG Owner, opt-in and never a production fallback.
add_executable(actions_c11_orch_dag_shell_process_test
    orch_dag_shell_process_test.c)
target_link_libraries(actions_c11_orch_dag_shell_process_test PRIVATE
    praktor_c11_orch_session_plan
    praktor_workflow_snapshot praktor_workflow_dag_state
    praktor_workflow_task_registry
    praktor_c11_workflow_snapshot_values
    praktor_c11_workflow_parallel_process_guard
    praktor_c11_native_host
    CMeta::CFlow Salts::Core Salts::TinyTest)
set_target_properties(actions_c11_orch_dag_shell_process_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "actions/tests")
add_test(NAME actions_c11_orch_dag_shell_process_test
    COMMAND actions_c11_orch_dag_shell_process_test)
set_tests_properties(actions_c11_orch_dag_shell_process_test
    PROPERTIES TIMEOUT 45)

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
    add_executable(actions_c11_${test} ${test}.c)
    target_link_libraries(actions_c11_${test} PRIVATE ${_deps} Salts::TinyTest)
    set_target_properties(actions_c11_${test} PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
    add_test(NAME actions_c11_${test} COMMAND actions_c11_${test})
endforeach()

add_executable(actions_output_scan_test output_scan_test.c)
target_include_directories(actions_output_scan_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core)
target_link_libraries(actions_output_scan_test PRIVATE praktor_output_core Salts::TinyTest)
set_target_properties(actions_output_scan_test PROPERTIES FOLDER "actions/tests")
add_test(NAME actions_output_scan_test COMMAND actions_output_scan_test)
