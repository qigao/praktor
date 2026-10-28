add_executable(yaml_value_c_test ${CMAKE_CURRENT_LIST_DIR}/../../test/yaml_value_test.c)
target_link_libraries(yaml_value_c_test PRIVATE praktor_yaml_value Salts::TinyTest Salts::Platform)
set_target_properties(yaml_value_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_value_c_test COMMAND yaml_value_c_test)
add_executable(yaml_value_fault_test ${CMAKE_CURRENT_LIST_DIR}/../../test/yaml_value_fault_test.c)
target_include_directories(yaml_value_fault_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(yaml_value_fault_test PRIVATE Salts::JsonParser Salts::CYaml Salts::Core Salts::TinyTest)
set_target_properties(yaml_value_fault_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_value_fault_test COMMAND yaml_value_fault_test)
