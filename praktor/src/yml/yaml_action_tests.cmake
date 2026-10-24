add_executable(yaml_action_c_test ${CMAKE_CURRENT_LIST_DIR}/../../test/yaml_action_test.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/process/src/process_control.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../actions/core/output_scan.c)
target_link_libraries(yaml_action_c_test PRIVATE praktor_yaml_action_params praktor_orch_tree Salts::TinyTest Salts::Platform)
target_compile_definitions(yaml_action_c_test PRIVATE
    PRAKTOR_YAML_ACTION_CORPUS_DIR="${CMAKE_CURRENT_LIST_DIR}/../../test/data")
set_target_properties(yaml_action_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_action_c_test COMMAND yaml_action_c_test)
add_executable(yaml_action_fault_test ${CMAKE_CURRENT_LIST_DIR}/../../test/yaml_action_fault_test.c)
target_include_directories(yaml_action_fault_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../include
    ${CMAKE_CURRENT_LIST_DIR}/../../../shared/process/include)
target_link_libraries(yaml_action_fault_test PRIVATE Salts::CSTL Salts::YamlParser Salts::Core Salts::TinyTest)
set_target_properties(yaml_action_fault_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_action_fault_test COMMAND yaml_action_fault_test)
