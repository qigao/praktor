add_executable(yaml_document_c_test ${CMAKE_CURRENT_LIST_DIR}/../../test/yaml_document_test.c)
target_link_libraries(yaml_document_c_test PRIVATE praktor_yaml_document Salts::TinyTest Salts::Platform)
set_target_properties(yaml_document_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_document_c_test COMMAND yaml_document_c_test)
add_executable(yaml_document_fault_test ${CMAKE_CURRENT_LIST_DIR}/../../test/yaml_document_fault_test.c)
target_include_directories(yaml_document_fault_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(yaml_document_fault_test PRIVATE Salts::YamlParser Salts::Core Salts::TinyTest)
set_target_properties(yaml_document_fault_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_document_fault_test COMMAND yaml_document_fault_test)
