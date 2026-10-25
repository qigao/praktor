add_executable(orch_yaml_c_test ${CMAKE_CURRENT_LIST_DIR}/../../test/orch_yaml_test.c)
target_link_libraries(orch_yaml_c_test PRIVATE praktor_orch_yaml Salts::TinyTest)
set_target_properties(orch_yaml_c_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME orch_yaml_c_test COMMAND orch_yaml_c_test)
add_executable(orch_yaml_fault_test ${CMAKE_CURRENT_LIST_DIR}/../../test/orch_yaml_fault_test.c)
target_include_directories(orch_yaml_fault_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(orch_yaml_fault_test PRIVATE
    praktor_orch_tree praktor_yaml_document Salts::TinyTest)
set_target_properties(orch_yaml_fault_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME orch_yaml_fault_test COMMAND orch_yaml_fault_test)
add_executable(orch_workflow_c_test ${CMAKE_CURRENT_LIST_DIR}/../../test/orch_workflow_c_test.c)
target_include_directories(orch_workflow_c_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(orch_workflow_c_test PRIVATE
    praktor_orch_yaml praktor_yaml_task_metadata
    praktor_yaml_workflow_metadata praktor_workflow_dependencies Salts::TinyTest)
set_target_properties(orch_workflow_c_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME orch_workflow_c_test COMMAND orch_workflow_c_test)
add_executable(workflow_dependencies_fault_test
    ${CMAKE_CURRENT_LIST_DIR}/../../test/workflow_dependencies_fault_test.c)
target_include_directories(workflow_dependencies_fault_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(workflow_dependencies_fault_test PRIVATE
    Salts::JsonParser Salts::TinyTest)
set_target_properties(workflow_dependencies_fault_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_dependencies_fault_test COMMAND workflow_dependencies_fault_test)
add_executable(workflow_snapshot_c_test
    ${CMAKE_CURRENT_LIST_DIR}/../../test/workflow_snapshot_test.c)
target_include_directories(workflow_snapshot_c_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(workflow_snapshot_c_test PRIVATE
    praktor_workflow_snapshot Salts::TinyTest)
set_target_properties(workflow_snapshot_c_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_snapshot_c_test COMMAND workflow_snapshot_c_test)
add_executable(workflow_snapshot_fault_test
    ${CMAKE_CURRENT_LIST_DIR}/../../test/workflow_snapshot_fault_test.c)
target_include_directories(workflow_snapshot_fault_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../include)
# This TU includes the snapshot C implementation with test-only fault hooks,
# so link constituent C targets, not praktor_workflow_snapshot itself.
target_link_libraries(workflow_snapshot_fault_test PRIVATE
    praktor_yaml_document praktor_yaml_workflow_metadata
    praktor_yaml_task_metadata praktor_yaml_action_params
    praktor_orch_yaml praktor_workflow_dependencies Salts::TinyTest)
set_target_properties(workflow_snapshot_fault_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_snapshot_fault_test COMMAND workflow_snapshot_fault_test)
add_executable(workflow_task_registry_c_test
    ${CMAKE_CURRENT_LIST_DIR}/../../test/workflow_task_registry_test.c)
target_include_directories(workflow_task_registry_c_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(workflow_task_registry_c_test PRIVATE
    praktor_workflow_task_registry Salts::TinyTest)
set_target_properties(workflow_task_registry_c_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_task_registry_c_test COMMAND workflow_task_registry_c_test)
