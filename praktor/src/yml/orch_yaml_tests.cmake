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
