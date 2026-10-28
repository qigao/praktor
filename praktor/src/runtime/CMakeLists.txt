add_library(praktor_runtime STATIC runtime.c component_host.c)
target_include_directories(praktor_runtime PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/praktor/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(praktor_runtime PUBLIC
    Salts::CMeta Salts::Configurator Salts::CFlow
    praktor_workflow_snapshot praktor_workflow_dag_state
    praktor_workflow_task_registry praktor_c11_orch_session_plan
    praktor_c11_native_host praktor_c11_workflow_snapshot_values
    praktor_c11_workflow_parallel_process_guard
    praktor_process_driver Salts::Process)
target_compile_features(praktor_runtime PUBLIC c_std_11)
set_target_properties(praktor_runtime PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
