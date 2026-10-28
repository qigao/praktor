# Canonical C-owned YAML, metadata, AST, typed state and DAG.
foreach(targets IN ITEMS
    src/expressions/lexer_targets.cmake
    src/expressions/evaluator_targets.cmake
    src/data/query_targets.cmake
    src/data/value_targets.cmake
    src/data/workflow_contract_value_targets.cmake
    src/yml/yaml_targets.cmake
    src/yml/yaml_value_targets.cmake
    src/yml/yaml_contract_targets.cmake
    src/yml/yaml_action_targets.cmake
    src/yml/yaml_task_targets.cmake
    src/yml/yaml_workflow_metadata_targets.cmake
    src/yml/yaml_task_metadata_targets.cmake
    src/yml/workflow_dependencies_targets.cmake
    src/yml/workflow_snapshot_targets.cmake
    src/yml/workflow_task_registry_targets.cmake
    src/yml/orch_targets.cmake
    src/yml/orch_yaml_targets.cmake
    src/profile/profile_targets.cmake
    src/util/file_hash_targets.cmake
    src/util/praktor_init_targets.cmake)
    include("${CMAKE_CURRENT_SOURCE_DIR}/${targets}")
endforeach()
