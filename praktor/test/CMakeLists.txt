# Actions
add_executable(actions_workflow_snapshot_values_test
    ${CMAKE_CURRENT_SOURCE_DIR}/actions/workflow_snapshot_values_test.c)
target_sources(actions_workflow_snapshot_values_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_snapshot_values.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/native_host.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/tree_adapter.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/output_scan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/regex_tre.c)
target_link_libraries(actions_workflow_snapshot_values_test PRIVATE
    Salts::CYaml Salts::Core CMeta::CSTL Salts::JsonParser Salts::BehaviorTree unofficial::tre::tre Salts::TinyTest)
set_target_properties(actions_workflow_snapshot_values_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "praktor/test/actions")
add_test(NAME actions_workflow_snapshot_values_test
    COMMAND actions_workflow_snapshot_values_test)

# Compiles the same C11 handoff with test-only failed allocation,
# clone and atomic batch publication boundaries; no C++ executor.
add_executable(actions_workflow_snapshot_values_fault_test
    ${CMAKE_CURRENT_SOURCE_DIR}/actions/workflow_snapshot_values_fault_test.c)
target_sources(actions_workflow_snapshot_values_fault_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c)
target_link_libraries(actions_workflow_snapshot_values_fault_test PRIVATE
    Salts::CYaml Salts::Core CMeta::CSTL Salts::JsonParser Salts::TinyTest)
set_target_properties(actions_workflow_snapshot_values_fault_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "praktor/test/actions")
add_test(NAME actions_workflow_snapshot_values_fault_test
    COMMAND actions_workflow_snapshot_values_fault_test)

add_executable(actions_workflow_parallel_commit_test
    ${CMAKE_CURRENT_SOURCE_DIR}/actions/workflow_parallel_commit_test.c)
target_sources(actions_workflow_parallel_commit_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_commit.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_task_registry.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c)
target_link_libraries(actions_workflow_parallel_commit_test PRIVATE
    Salts::CYaml Salts::Core CMeta::CSTL Salts::JsonParser Salts::TinyTest)
set_target_properties(actions_workflow_parallel_commit_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "praktor/test/actions")
add_test(NAME actions_workflow_parallel_commit_test
    COMMAND actions_workflow_parallel_commit_test)

# Test-only interposition of value_txn_commit_group: the real typed
# owner is staged before the failed root group and never published.
add_executable(actions_workflow_parallel_commit_fault_test
    ${CMAKE_CURRENT_SOURCE_DIR}/actions/workflow_parallel_commit_fault_test.c)
target_sources(actions_workflow_parallel_commit_fault_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_task_registry.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c)
target_link_libraries(actions_workflow_parallel_commit_fault_test PRIVATE
    Salts::CYaml Salts::Core CMeta::CSTL Salts::JsonParser Salts::TinyTest)
set_target_properties(actions_workflow_parallel_commit_fault_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "praktor/test/actions")
add_test(NAME actions_workflow_parallel_commit_fault_test
    COMMAND actions_workflow_parallel_commit_fault_test)

# Real Salts CFlow ManualExecutor is the ONLY owner of the task queue.
# This qualification verifies callback settlement before publication;
# the production scheduler and worker cancellation remain untouched.
add_executable(actions_workflow_parallel_executor_test
    ${CMAKE_CURRENT_SOURCE_DIR}/actions/workflow_parallel_executor_test.c)
target_sources(actions_workflow_parallel_executor_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_commit.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_task_registry.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c)
target_link_libraries(actions_workflow_parallel_executor_test PRIVATE
    Salts::CYaml Salts::Core CMeta::CSTL Salts::JsonParser CMeta::CFlow Salts::TinyTest)
set_target_properties(actions_workflow_parallel_executor_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "praktor/test/actions")
add_test(NAME actions_workflow_parallel_executor_test
    COMMAND actions_workflow_parallel_executor_test)

# Real concurrent Salts CFlow WorkerExecutor; no Praktor pool, and
# only the caller/owner publishes committed typed branch outputs.
add_executable(actions_workflow_parallel_worker_test
    ${CMAKE_CURRENT_SOURCE_DIR}/actions/workflow_parallel_worker_test.c)
target_sources(actions_workflow_parallel_worker_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_process_guard.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_owner_settlement.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_commit.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_task_registry.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dag_state.c)
target_link_libraries(actions_workflow_parallel_worker_test PRIVATE
    Salts::CYaml Salts::Core CMeta::CSTL Salts::JsonParser praktor_process_retainer CMeta::CFlow Salts::TinyTest)
set_target_properties(actions_workflow_parallel_worker_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "praktor/test/actions")
add_test(NAME actions_workflow_parallel_worker_test
    COMMAND actions_workflow_parallel_worker_test)

# Owner-only DAG readiness drives the canonical CFlow Manual/WorkerExecutor.
add_executable(actions_workflow_dag_cflow_test
    ${CMAKE_CURRENT_SOURCE_DIR}/actions/workflow_dag_cflow_test.c)
target_sources(actions_workflow_dag_cflow_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dag_state.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c)
target_link_libraries(actions_workflow_dag_cflow_test PRIVATE
    Salts::JsonParser CMeta::CFlow Salts::Core Salts::TinyTest)
set_target_properties(actions_workflow_dag_cflow_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "praktor/test/actions")
add_test(NAME actions_workflow_dag_cflow_test
    COMMAND actions_workflow_dag_cflow_test)
set_tests_properties(actions_workflow_dag_cflow_test
    PROPERTIES TIMEOUT 30)

# Real YAML Snapshot / DAG readiness, CFlow WorkerExecutor, root
# variables, typed TaskRegistry and native Process lease barrier.
# Deterministic callback effects only; no production leaf cutover.
add_executable(actions_workflow_dag_publication_test
    ${CMAKE_CURRENT_SOURCE_DIR}/actions/workflow_dag_publication_test.c)
target_sources(actions_workflow_dag_publication_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dag_state.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_snapshot_values.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_process_guard.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_owner_settlement.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_commit.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_task_registry.c)
target_link_libraries(actions_workflow_dag_publication_test PRIVATE
    Salts::JsonParser Salts::CYaml Salts::Core CMeta::CSTL praktor_process_retainer CMeta::CFlow Salts::TinyTest)
set_target_properties(actions_workflow_dag_publication_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "praktor/test/actions")
add_test(NAME actions_workflow_dag_publication_test
    COMMAND actions_workflow_dag_publication_test)
set_tests_properties(actions_workflow_dag_publication_test
    PROPERTIES TIMEOUT 30)

add_executable(actions_workflow_parallel_process_guard_test
    ${CMAKE_CURRENT_SOURCE_DIR}/actions/workflow_parallel_process_guard_test.c)
target_sources(actions_workflow_parallel_process_guard_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_process_guard.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_owner_settlement.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_commit.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_task_registry.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dag_state.c)
target_link_libraries(actions_workflow_parallel_process_guard_test PRIVATE
    Salts::CYaml Salts::Core CMeta::CSTL Salts::JsonParser praktor_process_retainer CMeta::CFlow Salts::TinyTest)
set_target_properties(actions_workflow_parallel_process_guard_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "praktor/test/actions")
add_test(NAME actions_workflow_parallel_process_guard_test
    COMMAND actions_workflow_parallel_process_guard_test)

# Only the Process recovery observer is faulted; real transactions and
# typed-output owners still use their canonical C11 implementations.
add_executable(actions_workflow_parallel_process_guard_fault_test
    ${CMAKE_CURRENT_SOURCE_DIR}/actions/workflow_parallel_process_guard_fault_test.c)
target_sources(actions_workflow_parallel_process_guard_fault_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_commit.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_task_registry.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c)
target_link_libraries(actions_workflow_parallel_process_guard_fault_test PRIVATE
    Salts::CYaml Salts::Core CMeta::CSTL Salts::JsonParser praktor_process_retainer CMeta::CFlow Salts::TinyTest)
set_target_properties(actions_workflow_parallel_process_guard_fault_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "praktor/test/actions")
add_test(NAME actions_workflow_parallel_process_guard_fault_test
    COMMAND actions_workflow_parallel_process_guard_fault_test)

# Real shell child / native Process lease handoff through the one
# existing C11 driver + Praktor recovery owner. Never force-free
# a live NativeIO completion payload to make the test succeed.
add_executable(actions_workflow_parallel_process_driver_test
    ${CMAKE_CURRENT_SOURCE_DIR}/actions/workflow_parallel_process_driver_test.c)
target_sources(actions_workflow_parallel_process_driver_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_process_guard.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_owner_settlement.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_commit.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_task_registry.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dag_state.c)
target_link_libraries(actions_workflow_parallel_process_driver_test PRIVATE
    Salts::CYaml Salts::Core CMeta::CSTL Salts::JsonParser praktor_process_retainer CMeta::CFlow praktor_process_driver Salts::TinyTest)
set_target_properties(actions_workflow_parallel_process_driver_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "praktor/test/actions")
add_test(NAME actions_workflow_parallel_process_driver_test
    COMMAND actions_workflow_parallel_process_driver_test)
set_tests_properties(actions_workflow_parallel_process_driver_test
    PROPERTIES TIMEOUT 25)

add_executable(actions_orch_session_plan_test
    ${CMAKE_CURRENT_SOURCE_DIR}/actions/orch_session_plan_test.c)
target_sources(actions_orch_session_plan_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/orch_session_plan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/plan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session_values.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session_controls.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/tree_adapter.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/native_host.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/output_scan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/regex_tre.c)
target_link_libraries(actions_orch_session_plan_test PRIVATE
    Salts::BehaviorTree praktor_process_driver Salts::JsonParser Salts::Core Salts::CYaml CMeta::CSTL unofficial::tre::tre Salts::TinyTest)
set_target_properties(actions_orch_session_plan_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "praktor/test/actions")
add_test(NAME actions_orch_session_plan_test
    COMMAND actions_orch_session_plan_test)
set_tests_properties(actions_orch_session_plan_test
    PROPERTIES TIMEOUT 30)

# Complete C11 YAML -> Orch -> Session -> owner-driven ManualExecutor
# DAG qualification. No worker-thread Session transfer or Legacy path.
add_executable(actions_orch_dag_native_session_test
    ${CMAKE_CURRENT_SOURCE_DIR}/actions/orch_dag_native_session_test.c)
target_sources(actions_orch_dag_native_session_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/orch_session_plan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/plan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session_values.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session_controls.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/tree_adapter.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dag_state.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_task_registry.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_snapshot_values.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_process_guard.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_owner_settlement.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_commit.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/native_host.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/output_scan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/regex_tre.c)
target_link_libraries(actions_orch_dag_native_session_test PRIVATE
    Salts::BehaviorTree praktor_process_driver Salts::JsonParser Salts::Core Salts::CYaml CMeta::CSTL praktor_process_retainer CMeta::CFlow unofficial::tre::tre Salts::TinyTest)
set_target_properties(actions_orch_dag_native_session_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "praktor/test/actions")
add_test(NAME actions_orch_dag_native_session_test
    COMMAND actions_orch_dag_native_session_test)
set_tests_properties(actions_orch_dag_native_session_test
    PROPERTIES TIMEOUT 30)

# WorkerExecutor owns the Session ONLY for one run callback; it
# returns bounded immutable results to the exclusive DAG Owner.
# This does not enable Shell/Process Session migration or production.
add_executable(actions_orch_worker_session_test
    ${CMAKE_CURRENT_SOURCE_DIR}/actions/orch_worker_session_test.c)
target_sources(actions_orch_worker_session_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/orch_session_plan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/plan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session_values.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session_controls.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/tree_adapter.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dag_state.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_task_registry.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_snapshot_values.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_process_guard.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_owner_settlement.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_commit.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/native_host.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/output_scan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/regex_tre.c)
target_link_libraries(actions_orch_worker_session_test PRIVATE
    Salts::BehaviorTree praktor_process_driver Salts::JsonParser Salts::Core Salts::CYaml CMeta::CSTL praktor_process_retainer CMeta::CFlow unofficial::tre::tre Salts::TinyTest)
set_target_properties(actions_orch_worker_session_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "praktor/test/actions")
add_test(NAME actions_orch_worker_session_test
    COMMAND actions_orch_worker_session_test)
set_tests_properties(actions_orch_worker_session_test
    PROPERTIES TIMEOUT 45)

# Real Shell/NativeIO in a worker callback, with the same retainer
# transferred to the application Owner ONLY after CFlow finalization.
# This is a test-only lifecycle, not a production scheduler switch.
add_executable(actions_orch_worker_process_test
    ${CMAKE_CURRENT_SOURCE_DIR}/actions/orch_worker_process_test.c)
target_sources(actions_orch_worker_process_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/orch_session_plan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/plan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session_values.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session_controls.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/tree_adapter.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dag_state.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_task_registry.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_snapshot_values.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_process_guard.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_owner_settlement.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_commit.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/native_host.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/output_scan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/regex_tre.c)
target_link_libraries(actions_orch_worker_process_test PRIVATE
    Salts::BehaviorTree praktor_process_driver Salts::JsonParser Salts::Core Salts::CYaml CMeta::CSTL praktor_process_retainer CMeta::CFlow unofficial::tre::tre Salts::TinyTest)
set_target_properties(actions_orch_worker_process_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "praktor/test/actions")
add_test(NAME actions_orch_worker_process_test
    COMMAND actions_orch_worker_process_test)
set_tests_properties(actions_orch_worker_process_test
    PROPERTIES TIMEOUT 45)

# Real canonical flat Shell → C11 Session → SaltsBase NativeIO →
# retainer → DAG Owner, opt-in and never a production fallback.
add_executable(actions_orch_dag_shell_process_test
    ${CMAKE_CURRENT_SOURCE_DIR}/actions/orch_dag_shell_process_test.c)
target_sources(actions_orch_dag_shell_process_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/orch_session_plan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/plan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session_values.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session_controls.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/tree_adapter.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dag_state.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_task_registry.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_snapshot_values.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_process_guard.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_owner_settlement.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_commit.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/native_host.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/output_scan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/regex_tre.c)
target_link_libraries(actions_orch_dag_shell_process_test PRIVATE
    Salts::BehaviorTree praktor_process_driver Salts::JsonParser Salts::Core Salts::CYaml CMeta::CSTL praktor_process_retainer CMeta::CFlow unofficial::tre::tre Salts::TinyTest)
set_target_properties(actions_orch_dag_shell_process_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    LINKER_LANGUAGE C FOLDER "praktor/test/actions")
add_test(NAME actions_orch_dag_shell_process_test
    COMMAND actions_orch_dag_shell_process_test)
set_tests_properties(actions_orch_dag_shell_process_test
    PROPERTIES TIMEOUT 45)

add_executable(actions_session_c_test ${CMAKE_CURRENT_SOURCE_DIR}/actions/session_c_test.c)
target_sources(actions_session_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/plan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session_values.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session_controls.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/tree_adapter.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/native_host.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/output_scan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/regex_tre.c)
target_link_libraries(actions_session_c_test PRIVATE
    Salts::BehaviorTree praktor_process_driver Salts::JsonParser Salts::Core unofficial::tre::tre Salts::TinyTest)
set_target_properties(actions_session_c_test PROPERTIES FOLDER "praktor/tests")
add_test(NAME actions_session_c_test COMMAND actions_session_c_test)
add_executable(actions_native_host_test ${CMAKE_CURRENT_SOURCE_DIR}/actions/native_host_test.c)
target_sources(actions_native_host_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/native_host.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/tree_adapter.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/output_scan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/regex_tre.c)
target_link_libraries(actions_native_host_test PRIVATE
    Salts::BehaviorTree Salts::Core Salts::JsonParser unofficial::tre::tre Salts::TinyTest)
set_target_properties(actions_native_host_test PROPERTIES FOLDER "praktor/tests")
add_test(NAME actions_native_host_test COMMAND actions_native_host_test)
add_executable(actions_value_store_test ${CMAKE_CURRENT_SOURCE_DIR}/actions/value_store_test.c)
target_sources(actions_value_store_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c)
target_link_libraries(actions_value_store_test PRIVATE
    Salts::TinyTest)
set_target_properties(actions_value_store_test PROPERTIES FOLDER "praktor/tests")
add_test(NAME actions_value_store_test COMMAND actions_value_store_test)
add_executable(actions_regex_tre_test ${CMAKE_CURRENT_SOURCE_DIR}/actions/regex_tre_test.c)
target_sources(actions_regex_tre_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/regex_tre.c)
target_link_libraries(actions_regex_tre_test PRIVATE
    unofficial::tre::tre Salts::TinyTest)
set_target_properties(actions_regex_tre_test PROPERTIES FOLDER "praktor/tests")
add_test(NAME actions_regex_tre_test COMMAND actions_regex_tre_test)
add_executable(actions_tree_adapter_test ${CMAKE_CURRENT_SOURCE_DIR}/actions/tree_adapter_test.c)
target_sources(actions_tree_adapter_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/tree_adapter.c)
target_link_libraries(actions_tree_adapter_test PRIVATE
    Salts::BehaviorTree Salts::TinyTest)
set_target_properties(actions_tree_adapter_test PROPERTIES FOLDER "praktor/tests")
add_test(NAME actions_tree_adapter_test COMMAND actions_tree_adapter_test)
add_executable(actions_plan_test ${CMAKE_CURRENT_SOURCE_DIR}/actions/plan_test.c)
target_sources(actions_plan_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/plan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session_values.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session_controls.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/tree_adapter.c)
target_link_libraries(actions_plan_test PRIVATE
    Salts::BehaviorTree praktor_process_driver Salts::JsonParser Salts::TinyTest)
set_target_properties(actions_plan_test PROPERTIES FOLDER "praktor/tests")
add_test(NAME actions_plan_test COMMAND actions_plan_test)


add_executable(actions_output_scan_test ${CMAKE_CURRENT_SOURCE_DIR}/actions/output_scan_test.c)
target_sources(actions_output_scan_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/output_scan.c)
target_link_libraries(actions_output_scan_test PRIVATE
    Salts::TinyTest)
set_target_properties(actions_output_scan_test PROPERTIES FOLDER "praktor/test/actions")
add_test(NAME actions_output_scan_test COMMAND actions_output_scan_test)

# test
# Formal C11 module and application tests.
add_executable(expression_lexer_test ${CMAKE_CURRENT_SOURCE_DIR}/expression_lexer_test.c)
target_sources(expression_lexer_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/expressions/expression_lexer.c)
target_link_libraries(expression_lexer_test PRIVATE
    Salts::Core Salts::TinyTest)
set_target_properties(expression_lexer_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME expression_lexer_test COMMAND expression_lexer_test)

add_executable(expression_evaluator_c_test ${CMAKE_CURRENT_SOURCE_DIR}/expression_evaluator_test.c)
target_sources(expression_evaluator_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/expressions/expression_evaluator.c
    ${PROJECT_SOURCE_DIR}/praktor/src/expressions/expression_lexer.c
    ${PROJECT_SOURCE_DIR}/praktor/src/expressions/expression_regex_salts.c)
target_link_libraries(expression_evaluator_c_test PRIVATE
    Salts::Core Salts::TinyTest)
set_target_properties(expression_evaluator_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME expression_evaluator_c_test COMMAND expression_evaluator_c_test)

add_executable(structured_document_query_c_test ${CMAKE_CURRENT_SOURCE_DIR}/structured_document_query_test.c)
target_sources(structured_document_query_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/data/structured_document_query.c)
target_link_libraries(structured_document_query_c_test PRIVATE
    Salts::JsonParser Salts::Core Salts::CYaml Salts::CYamlJsonAdapter Salts::XmlParser Salts::CsvParser Salts::TinyTest)
set_target_properties(structured_document_query_c_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME structured_document_query_c_test COMMAND structured_document_query_c_test)

add_executable(workflow_value_c_test ${CMAKE_CURRENT_SOURCE_DIR}/workflow_value_test.c)
target_sources(workflow_value_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/data/workflow_value.c)
target_link_libraries(workflow_value_c_test PRIVATE
    Salts::JsonParser Salts::Core Salts::TinyTest)
set_target_properties(workflow_value_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_value_c_test COMMAND workflow_value_c_test)
add_executable(workflow_value_fault_test ${CMAKE_CURRENT_SOURCE_DIR}/workflow_value_fault_test.c)
target_link_libraries(workflow_value_fault_test PRIVATE Salts::JsonParser Salts::Core Salts::TinyTest)
target_include_directories(workflow_value_fault_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
if(UNIX)
    target_link_libraries(workflow_value_fault_test PRIVATE m)
endif()
set_target_properties(workflow_value_fault_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_value_fault_test COMMAND workflow_value_fault_test)

add_executable(workflow_contract_value_c_test
    ${CMAKE_CURRENT_SOURCE_DIR}/workflow_contract_value_test.c)
target_sources(workflow_contract_value_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/data/workflow_contract_value.c
    ${PROJECT_SOURCE_DIR}/praktor/src/data/workflow_value.c)
target_link_libraries(workflow_contract_value_c_test PRIVATE
    Salts::JsonParser Salts::Core Salts::TinyTest)
set_target_properties(workflow_contract_value_c_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_contract_value_c_test COMMAND workflow_contract_value_c_test)

add_executable(yaml_document_c_test ${CMAKE_CURRENT_SOURCE_DIR}/yaml_document_test.c)
target_sources(yaml_document_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c)
target_link_libraries(yaml_document_c_test PRIVATE
    Salts::CYaml Salts::Core Salts::TinyTest)
set_target_properties(yaml_document_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_document_c_test COMMAND yaml_document_c_test)
add_executable(yaml_document_fault_test ${CMAKE_CURRENT_SOURCE_DIR}/yaml_document_fault_test.c)
target_include_directories(yaml_document_fault_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(yaml_document_fault_test PRIVATE Salts::CYaml Salts::Core Salts::TinyTest)
set_target_properties(yaml_document_fault_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_document_fault_test COMMAND yaml_document_fault_test)
add_executable(yaml_fields_c_test ${CMAKE_CURRENT_SOURCE_DIR}/yaml_fields_test.c)
target_sources(yaml_fields_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c)
target_link_libraries(yaml_fields_c_test PRIVATE
    Salts::CYaml Salts::Core Salts::TinyTest)
set_target_properties(yaml_fields_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
target_compile_definitions(yaml_fields_c_test PRIVATE
    PRAKTOR_YAML_FIELD_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME yaml_fields_c_test COMMAND yaml_fields_c_test)
add_executable(yaml_fields_fault_test ${CMAKE_CURRENT_SOURCE_DIR}/yaml_fields_fault_test.c)
target_include_directories(yaml_fields_fault_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(yaml_fields_fault_test PRIVATE Salts::CYaml Salts::Core Salts::TinyTest)
set_target_properties(yaml_fields_fault_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_fields_fault_test COMMAND yaml_fields_fault_test)

add_executable(yaml_value_c_test ${CMAKE_CURRENT_SOURCE_DIR}/yaml_value_test.c)
target_sources(yaml_value_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_value.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c)
target_link_libraries(yaml_value_c_test PRIVATE
    Salts::CYaml Salts::Core Salts::JsonParser Salts::TinyTest)
set_target_properties(yaml_value_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_value_c_test COMMAND yaml_value_c_test)
add_executable(yaml_value_fault_test ${CMAKE_CURRENT_SOURCE_DIR}/yaml_value_fault_test.c)
target_include_directories(yaml_value_fault_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(yaml_value_fault_test PRIVATE Salts::JsonParser Salts::CYaml Salts::Core Salts::TinyTest)
set_target_properties(yaml_value_fault_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_value_fault_test COMMAND yaml_value_fault_test)

add_executable(yaml_contract_c_test ${CMAKE_CURRENT_SOURCE_DIR}/yaml_contract_test.c)
target_sources(yaml_contract_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_contract.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_value.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/data/workflow_contract_value.c
    ${PROJECT_SOURCE_DIR}/praktor/src/data/workflow_value.c)
target_link_libraries(yaml_contract_c_test PRIVATE
    Salts::CYaml Salts::Core Salts::JsonParser Salts::TinyTest)
set_target_properties(yaml_contract_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_contract_c_test COMMAND yaml_contract_c_test)

add_executable(yaml_action_c_test ${CMAKE_CURRENT_SOURCE_DIR}/yaml_action_test.c)
target_sources(yaml_action_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/output_scan.c)
target_link_libraries(yaml_action_c_test PRIVATE
    Salts::CYaml Salts::Core praktor_process_native Salts::TinyTest)
target_compile_definitions(yaml_action_c_test PRIVATE
    PRAKTOR_YAML_ACTION_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
set_target_properties(yaml_action_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_action_c_test COMMAND yaml_action_c_test)
add_executable(yaml_action_fault_test ${CMAKE_CURRENT_SOURCE_DIR}/yaml_action_fault_test.c)
target_include_directories(yaml_action_fault_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${PROJECT_SOURCE_DIR}/shared/process/include)
target_link_libraries(yaml_action_fault_test PRIVATE CMeta::CSTL Salts::CYaml Salts::Core Salts::TinyTest)
set_target_properties(yaml_action_fault_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_action_fault_test COMMAND yaml_action_fault_test)

add_executable(yaml_task_fields_c_test ${CMAKE_CURRENT_SOURCE_DIR}/yaml_task_fields_test.c)
target_sources(yaml_task_fields_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c)
target_link_libraries(yaml_task_fields_c_test PRIVATE
    Salts::CYaml Salts::Core CMeta::CSTL Salts::TinyTest)
set_target_properties(yaml_task_fields_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_task_fields_c_test COMMAND yaml_task_fields_c_test)
add_executable(yaml_task_fields_fault_test ${CMAKE_CURRENT_SOURCE_DIR}/yaml_task_fields_fault_test.c)
target_include_directories(yaml_task_fields_fault_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(yaml_task_fields_fault_test PRIVATE CMeta::CSTL Salts::CYaml Salts::Core Salts::TinyTest)
set_target_properties(yaml_task_fields_fault_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_task_fields_fault_test COMMAND yaml_task_fields_fault_test)

add_executable(yaml_workflow_metadata_c_test ${CMAKE_CURRENT_SOURCE_DIR}/yaml_workflow_metadata_test.c)
target_sources(yaml_workflow_metadata_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c)
target_link_libraries(yaml_workflow_metadata_c_test PRIVATE
    Salts::CYaml Salts::Core CMeta::CSTL Salts::JsonParser Salts::TinyTest)
set_target_properties(yaml_workflow_metadata_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_workflow_metadata_c_test COMMAND yaml_workflow_metadata_c_test)

add_executable(yaml_task_metadata_c_test ${CMAKE_CURRENT_SOURCE_DIR}/yaml_task_metadata_test.c)
target_sources(yaml_task_metadata_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c)
target_link_libraries(yaml_task_metadata_c_test PRIVATE
    Salts::CYaml Salts::Core CMeta::CSTL Salts::JsonParser Salts::TinyTest)
set_target_properties(yaml_task_metadata_c_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_task_metadata_c_test COMMAND yaml_task_metadata_c_test)

add_executable(orch_tree_c_test ${CMAKE_CURRENT_SOURCE_DIR}/orch_tree_test.c)
target_sources(orch_tree_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c)
target_link_libraries(orch_tree_c_test PRIVATE
    Salts::Core Salts::TinyTest)
set_target_properties(orch_tree_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME orch_tree_c_test COMMAND orch_tree_c_test)
add_executable(orch_tree_fault_test ${CMAKE_CURRENT_SOURCE_DIR}/orch_tree_fault_test.c)
target_include_directories(orch_tree_fault_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(orch_tree_fault_test PRIVATE Salts::Core Salts::TinyTest)
set_target_properties(orch_tree_fault_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME orch_tree_fault_test COMMAND orch_tree_fault_test)

add_executable(orch_yaml_c_test ${CMAKE_CURRENT_SOURCE_DIR}/orch_yaml_test.c)
target_sources(orch_yaml_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c)
target_link_libraries(orch_yaml_c_test PRIVATE
    Salts::Core Salts::CYaml Salts::TinyTest)
set_target_properties(orch_yaml_c_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME orch_yaml_c_test COMMAND orch_yaml_c_test)
add_executable(orch_yaml_fault_test ${CMAKE_CURRENT_SOURCE_DIR}/orch_yaml_fault_test.c)
target_include_directories(orch_yaml_fault_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_sources(orch_yaml_fault_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c)
target_link_libraries(orch_yaml_fault_test PRIVATE
    Salts::Core Salts::CYaml Salts::TinyTest)
set_target_properties(orch_yaml_fault_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME orch_yaml_fault_test COMMAND orch_yaml_fault_test)
add_executable(orch_workflow_c_test ${CMAKE_CURRENT_SOURCE_DIR}/orch_workflow_c_test.c)
target_include_directories(orch_workflow_c_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_sources(orch_workflow_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c)
target_link_libraries(orch_workflow_c_test PRIVATE
    Salts::Core Salts::CYaml CMeta::CSTL Salts::JsonParser Salts::TinyTest)
set_target_properties(orch_workflow_c_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME orch_workflow_c_test COMMAND orch_workflow_c_test)
add_executable(workflow_dependencies_fault_test
    ${CMAKE_CURRENT_SOURCE_DIR}/workflow_dependencies_fault_test.c)
target_include_directories(workflow_dependencies_fault_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(workflow_dependencies_fault_test PRIVATE
    Salts::JsonParser Salts::TinyTest)
set_target_properties(workflow_dependencies_fault_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_dependencies_fault_test COMMAND workflow_dependencies_fault_test)
add_executable(workflow_snapshot_c_test
    ${CMAKE_CURRENT_SOURCE_DIR}/workflow_snapshot_test.c)
target_include_directories(workflow_snapshot_c_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_sources(workflow_snapshot_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c)
target_link_libraries(workflow_snapshot_c_test PRIVATE
    Salts::CYaml Salts::Core CMeta::CSTL Salts::JsonParser Salts::TinyTest)
set_target_properties(workflow_snapshot_c_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_snapshot_c_test COMMAND workflow_snapshot_c_test)
add_executable(workflow_snapshot_fault_test
    ${CMAKE_CURRENT_SOURCE_DIR}/workflow_snapshot_fault_test.c)
target_include_directories(workflow_snapshot_fault_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
# This TU includes the snapshot C implementation with test-only fault hooks,
# so link constituent C targets, not praktor_workflow_snapshot itself.
target_sources(workflow_snapshot_fault_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c)
target_link_libraries(workflow_snapshot_fault_test PRIVATE
    Salts::CYaml Salts::Core CMeta::CSTL Salts::JsonParser Salts::TinyTest)
set_target_properties(workflow_snapshot_fault_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_snapshot_fault_test COMMAND workflow_snapshot_fault_test)
add_executable(workflow_task_registry_c_test
    ${CMAKE_CURRENT_SOURCE_DIR}/workflow_task_registry_test.c)
target_include_directories(workflow_task_registry_c_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_sources(workflow_task_registry_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_task_registry.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c)
target_link_libraries(workflow_task_registry_c_test PRIVATE
    Salts::CYaml Salts::Core CMeta::CSTL Salts::JsonParser Salts::TinyTest)
set_target_properties(workflow_task_registry_c_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_task_registry_c_test COMMAND workflow_task_registry_c_test)
add_executable(workflow_task_registry_fault_test
    ${CMAKE_CURRENT_SOURCE_DIR}/workflow_task_registry_fault_test.c)
target_include_directories(workflow_task_registry_fault_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
# This TU includes the C11 owner implementation with test-only boundaries.
# Link canonical Snapshot + JsonParser, not the duplicate owner object code.
target_sources(workflow_task_registry_fault_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c)
target_link_libraries(workflow_task_registry_fault_test PRIVATE
    Salts::CYaml Salts::Core CMeta::CSTL Salts::JsonParser Salts::TinyTest)
set_target_properties(workflow_task_registry_fault_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_task_registry_fault_test
    COMMAND workflow_task_registry_fault_test)

add_executable(workflow_dag_state_c_test
    ${CMAKE_CURRENT_SOURCE_DIR}/workflow_dag_state_test.c)
target_sources(workflow_dag_state_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dag_state.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c)
target_link_libraries(workflow_dag_state_c_test PRIVATE
    Salts::JsonParser Salts::TinyTest)
set_target_properties(workflow_dag_state_c_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_dag_state_c_test COMMAND workflow_dag_state_c_test)

add_executable(workflow_profile_c_test ${CMAKE_CURRENT_SOURCE_DIR}/workflow_profile_c_test.c)
target_sources(workflow_profile_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/profile/workflow_profile.c)
target_link_libraries(workflow_profile_c_test PRIVATE
    Salts::TinyTest Salts::Core)
set_target_properties(workflow_profile_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_profile_c_test COMMAND workflow_profile_c_test)

add_executable(praktor_file_hash_c_test ${CMAKE_CURRENT_SOURCE_DIR}/file_hash_c_test.c)
target_sources(praktor_file_hash_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/util/file_hash.c)
target_link_libraries(praktor_file_hash_c_test PRIVATE
    Salts::TinyTest Salts::Core)
set_target_properties(praktor_file_hash_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME praktor_file_hash_c_test COMMAND praktor_file_hash_c_test)

add_executable(praktor_init_c_test ${CMAKE_CURRENT_SOURCE_DIR}/praktor_init_c_test.c)
target_sources(praktor_init_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/util/praktor_init.c)
target_link_libraries(praktor_init_c_test PRIVATE
    Salts::Core CMeta::CSTL Salts::TinyTest)
set_target_properties(praktor_init_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME praktor_init_c_test COMMAND praktor_init_c_test)

add_executable(praktor_runtime_c_test ${CMAKE_CURRENT_SOURCE_DIR}/praktor_runtime_test.c)
target_sources(praktor_runtime_c_test PRIVATE
    ${PROJECT_SOURCE_DIR}/praktor/src/runtime/runtime.c
    ${PROJECT_SOURCE_DIR}/praktor/src/runtime/component_host.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_snapshot.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_document.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_workflow_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_fields.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_task_metadata.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/yaml_action_params.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_yaml.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/orch_tree.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dependencies.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_dag_state.c
    ${PROJECT_SOURCE_DIR}/praktor/src/yml/workflow_task_registry.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/orch_session_plan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/plan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session_values.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/session_controls.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/tree_adapter.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/native_host.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/value_store.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/output_scan.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/regex_tre.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_snapshot_values.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_process_guard.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_owner_settlement.c
    ${PROJECT_SOURCE_DIR}/praktor/src/actions/workflow_parallel_commit.c)
target_link_libraries(praktor_runtime_c_test PRIVATE
    CMeta::CMeta CMeta::ACE CMeta::CFlow Salts::CYaml Salts::Core CMeta::CSTL Salts::JsonParser Salts::BehaviorTree praktor_process_driver unofficial::tre::tre praktor_process_retainer Salts::NativeIO Salts::TinyTest)
set_target_properties(praktor_runtime_c_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME praktor_runtime_c_test COMMAND praktor_runtime_c_test)
set_tests_properties(praktor_runtime_c_test PROPERTIES TIMEOUT 45)

add_executable(praktor_c_api_test ${CMAKE_CURRENT_SOURCE_DIR}/praktor_c_api_test.c)
target_link_libraries(praktor_c_api_test PRIVATE Praktor Salts::TinyTest Salts::JsonParser)
add_test(NAME praktor_c_api_test COMMAND praktor_c_api_test)

add_executable(praktor_cli_test ${CMAKE_CURRENT_SOURCE_DIR}/praktor_cli_test.c)
target_link_libraries(praktor_cli_test PRIVATE Salts::Core Salts::JsonParser Salts::TinyTest)
target_compile_definitions(praktor_cli_test PRIVATE
    PRAKTOR_CLI_PATH="$<TARGET_FILE:praktor>"
    PRAKTOR_CLI_WORKFLOW="${CMAKE_CURRENT_SOURCE_DIR}/data/cli-shell.yml"
    PRAKTOR_CLI_TIMEOUT_WORKFLOW="${CMAKE_CURRENT_SOURCE_DIR}/data/cli-timeout.yml"
    PRAKTOR_CLI_ENV_WORKFLOW="${CMAKE_CURRENT_BINARY_DIR}/cli-env.yml")
if(WIN32)
    set(PRAKTOR_CLI_ENV_COMMAND "echo %PRAKTOR_CLI_ENV_TEST_VALUE%")
else()
    set(PRAKTOR_CLI_ENV_COMMAND "echo $PRAKTOR_CLI_ENV_TEST_VALUE")
endif()
configure_file(data/cli-env.yml.in cli-env.yml @ONLY)
add_dependencies(praktor_cli_test praktor)
add_test(NAME praktor_cli_test COMMAND praktor_cli_test)
set_tests_properties(praktor_cli_test PROPERTIES TIMEOUT 30)
set_target_properties(praktor_c_api_test praktor_cli_test PROPERTIES
    FOLDER "praktor/tests")

# White-box executables compile private implementation without exporting it
# through Praktor's public dynamic-library ABI. Faulted modules remain supplied
# by their existing test translation units rather than being linked twice.
set(_praktor_private_test_includes
    ${PROJECT_SOURCE_DIR}/praktor/include
    ${PROJECT_SOURCE_DIR}/shared/process/include
    ${PROJECT_SOURCE_DIR}/praktor/src/runtime
    ${PROJECT_SOURCE_DIR}/praktor/src/profile
    ${PROJECT_SOURCE_DIR}/praktor/src/util)
foreach(_target IN ITEMS
    actions_workflow_snapshot_values_test
    actions_workflow_snapshot_values_fault_test
    actions_workflow_parallel_commit_test
    actions_workflow_parallel_commit_fault_test
    actions_workflow_parallel_executor_test
    actions_workflow_parallel_worker_test
    actions_workflow_dag_cflow_test
    actions_workflow_dag_publication_test
    actions_workflow_parallel_process_guard_test
    actions_workflow_parallel_process_guard_fault_test
    actions_workflow_parallel_process_driver_test
    actions_orch_session_plan_test
    actions_orch_dag_native_session_test
    actions_orch_worker_session_test
    actions_orch_worker_process_test
    actions_orch_dag_shell_process_test
    actions_session_c_test
    actions_native_host_test
    actions_value_store_test
    actions_regex_tre_test
    actions_tree_adapter_test
    actions_plan_test
    actions_output_scan_test
    expression_lexer_test
    expression_evaluator_c_test
    structured_document_query_c_test
    workflow_value_c_test
    workflow_contract_value_c_test
    yaml_document_c_test
    yaml_fields_c_test
    yaml_value_c_test
    yaml_contract_c_test
    yaml_action_c_test
    yaml_task_fields_c_test
    yaml_workflow_metadata_c_test
    yaml_task_metadata_c_test
    orch_tree_c_test
    orch_yaml_c_test
    orch_yaml_fault_test
    orch_workflow_c_test
    workflow_snapshot_c_test
    workflow_snapshot_fault_test
    workflow_task_registry_c_test
    workflow_task_registry_fault_test
    workflow_dag_state_c_test
    workflow_profile_c_test
    praktor_file_hash_c_test
    praktor_init_c_test
    praktor_runtime_c_test)
    target_include_directories(${_target} PRIVATE ${_praktor_private_test_includes})
    if(UNIX)
        target_link_libraries(${_target} PRIVATE m)
    endif()
endforeach()
