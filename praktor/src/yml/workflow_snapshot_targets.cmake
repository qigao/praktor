# Composes existing C-owned metadata, graph and action AST constructors.
# This is a qualification-only inline workflow snapshot, not WorkflowPlan
# provenance, an executor, a scheduler or a new public installed SDK ABI.
add_library(praktor_workflow_snapshot STATIC
    ${CMAKE_CURRENT_LIST_DIR}/workflow_snapshot.c)
target_include_directories(praktor_workflow_snapshot PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(praktor_workflow_snapshot PUBLIC
    praktor_yaml_document
    praktor_yaml_workflow_metadata
    praktor_yaml_task_metadata
    praktor_yaml_action_params
    praktor_orch_yaml
    praktor_orch_tree
    praktor_workflow_dependencies
    Salts::JsonParser)
target_compile_features(praktor_workflow_snapshot PUBLIC c_std_11)
set_target_properties(praktor_workflow_snapshot PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
