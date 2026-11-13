add_executable(yaml_task_fields_c_test ${CMAKE_CURRENT_LIST_DIR}/yaml_task_fields_test.c)
target_link_libraries(yaml_task_fields_c_test PRIVATE praktor_yaml_task_fields Salts::TinyTest Salts::Core)
set_target_properties(yaml_task_fields_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_task_fields_c_test COMMAND yaml_task_fields_c_test)
add_executable(yaml_task_fields_fault_test ${CMAKE_CURRENT_LIST_DIR}/yaml_task_fields_fault_test.c)
target_include_directories(yaml_task_fields_fault_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include)
target_link_libraries(yaml_task_fields_fault_test PRIVATE CMeta::CSTL Salts::CYaml Salts::Core Salts::TinyTest)
set_target_properties(yaml_task_fields_fault_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_task_fields_fault_test COMMAND yaml_task_fields_fault_test)
