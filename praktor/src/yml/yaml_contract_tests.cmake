add_executable(yaml_contract_c_test ${CMAKE_CURRENT_LIST_DIR}/../../test/yaml_contract_test.c)
target_link_libraries(yaml_contract_c_test PRIVATE praktor_yaml_contract Salts::TinyTest)
set_target_properties(yaml_contract_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_contract_c_test COMMAND yaml_contract_c_test)
