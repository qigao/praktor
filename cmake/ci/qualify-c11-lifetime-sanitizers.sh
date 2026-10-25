#!/usr/bin/env bash
# Qualify the opt-in C11 Process/Workflow lifecycle without rebuilding every
# native dependency. This is a consumer-side ASan/UBSan or TSan slice, not
# proof that an uninstrumented installed NativeIO provider is race-free.
set -euo pipefail

mode="${1:?usage: qualify-c11-lifetime-sanitizers.sh address|thread}"
case "$mode" in
  address)
    sanitizer_flags="-fsanitize=address,undefined,float-cast-overflow"
    ;;
  thread)
    sanitizer_flags="-fsanitize=thread"
    ;;
  *)
    echo "Unsupported C11 sanitizer mode: $mode" >&2
    exit 2
    ;;
esac

: "${CMAKE_PRESET:?CMAKE_PRESET is required}"
build_dir="build/ci-c11-${mode}"
compile_flags="${sanitizer_flags} -fno-omit-frame-pointer -fno-optimize-sibling-calls -O1 -g"

cmake --preset "$CMAKE_PRESET" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_TESTS=ON \
  -DENABLE_SCRIPT_ENGINE=OFF \
  -DPRAKTOR_ENABLE_C11_ACTIONS=ON \
  "-DCMAKE_C_FLAGS=$compile_flags" \
  "-DCMAKE_CXX_FLAGS=$compile_flags" \
  "-DCMAKE_EXE_LINKER_FLAGS=$sanitizer_flags" \
  "-DCMAKE_SHARED_LINKER_FLAGS=$sanitizer_flags"

cmake --build "$build_dir" \
  --parallel 2
# Released Salts.Native is not TSan-instrumented. Consumer-only TSan
# cannot see its async logger's release/acquire edges and reports an invalid
# mixed-instrumentation race in tests exercising WorkflowRunner/Executor
# logging. WorkflowRunner and the application-control bridge tests are instead
# REQUIRED in .github/workflows/c11-source-tsan.yml with
# fully instrumented Salts + SaltsUtils and attested actual DSO linkage.
# The separately instrumented C11 Session smoke test remains useful here.
case "$mode" in
  address)
    test_regex='^(schema_projection_cli|schema_projection_test|schema_projection_cli_usage|expression_lexer_test|expression_evaluator_c_test|yaml_document_c_test|yaml_document_fault_test|yaml_document_legacy_fault_test|yaml_fields_c_test|yaml_fields_fault_test|yaml_value_c_test|yaml_value_fault_test|yaml_action_c_test|yaml_action_fault_test|yaml_task_fields_c_test|yaml_task_fields_fault_test|orch_tree_c_test|orch_tree_fault_test|orch_yaml_c_test|orch_yaml_fault_test|orch_workflow_c_test|workflow_dependencies_fault_test|workflow_dag_state_c_test|workflow_snapshot_c_test|workflow_snapshot_fault_test|workflow_task_registry_c_test|workflow_task_registry_fault_test|actions_c11_workflow_snapshot_values_test|actions_c11_workflow_snapshot_values_fault_test|actions_c11_workflow_parallel_commit_test|actions_c11_workflow_parallel_commit_fault_test|actions_c11_workflow_parallel_executor_test|actions_c11_workflow_parallel_worker_test|actions_c11_workflow_parallel_process_guard_test|actions_c11_workflow_parallel_process_guard_fault_test|actions_c11_workflow_parallel_process_driver_test|orch_yaml_differential_test[.]praktor_lib|workflow_profile_c_test|praktor_file_hash_c_test|praktor_init_c_test|workflow_contract_api_test\.Praktor|host_tool_api_test\.Praktor|workflow_value_c_test|workflow_value_fault_test|workflow_value_test\.praktor_lib|structured_document_query_c_test|structured_document_query_test\.praktor_lib|expression_evaluator_test\.praktor_lib|actions_output_core_test|test_output_parser|service_executor_test\.praktor_lib|managed_process_executor_test\.praktor_lib|shell_executor_test|praktor_process_driver_test|praktor_process_worker_test|praktor_process_control_test|praktor_process_native_test|praktor_service_control_test|praktor_service_process_test|actions_c11_tree_executor_session_test|actions_c11_session_c_test|actions_c11_tree_plan_test|actions_c11_session_independent_test|actions_c11_native_host_test|actions_c11_regex_tre_test|actions_c11_session_yaml_differential_test|core_only_workflow_test\.praktor_lib)$'
    ;;
  thread)
    test_regex='^(shell_executor_test|praktor_process_driver_test|praktor_process_worker_test|praktor_process_control_test|praktor_process_native_test|praktor_service_control_test|praktor_service_process_test|actions_c11_tree_executor_session_test|actions_c11_session_c_test|actions_c11_tree_plan_test|actions_c11_session_independent_test|actions_c11_native_host_test|actions_c11_regex_tre_test|workflow_dag_state_c_test)$'
    ;;
esac
ctest --test-dir "$build_dir" \
  --no-tests=error --output-on-failure --parallel 1 --timeout 75 \
  -R "$test_regex"

    ;;
esac
ctest --test-dir "$build_dir" \
  --no-tests=error --output-on-failure --parallel 1 --timeout 75 \
  -R "$test_regex"
