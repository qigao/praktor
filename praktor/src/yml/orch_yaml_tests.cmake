add_executable(orch_yaml_c_test ${CMAKE_CURRENT_LIST_DIR}/../../test/orch_yaml_test.c)
target_link_libraries(orch_yaml_c_test PRIVATE praktor_orch_yaml Salts::TinyTest)
set_target_properties(orch_yaml_c_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME orch_yaml_c_test COMMAND orch_yaml_c_test)
