add_executable(yaml_action_c_test ${CMAKE_CURRENT_LIST_DIR}/yaml_action_test.c)
target_link_libraries(yaml_action_c_test PRIVATE praktor_yaml_action_params praktor_orch_tree
    praktor_process_native praktor_output_core Salts::TinyTest Salts::Core)
target_compile_definitions(yaml_action_c_test PRIVATE
    PRAKTOR_YAML_ACTION_CORPUS_DIR="${CMAKE_CURRENT_LIST_DIR}/data")
set_target_properties(yaml_action_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_action_c_test COMMAND yaml_action_c_test)
add_executable(yaml_action_fault_test ${CMAKE_CURRENT_LIST_DIR}/yaml_action_fault_test.c)
target_include_directories(yaml_action_fault_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/process/include)
target_link_libraries(yaml_action_fault_test PRIVATE CMeta::CSTL Salts::CYaml Salts::Core Salts::TinyTest)
set_target_properties(yaml_action_fault_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_action_fault_test COMMAND yaml_action_fault_test)
