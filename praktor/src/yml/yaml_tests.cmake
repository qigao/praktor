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
add_executable(yaml_fields_c_test ${CMAKE_CURRENT_LIST_DIR}/../../test/yaml_fields_test.c)
target_link_libraries(yaml_fields_c_test PRIVATE praktor_yaml_document Salts::TinyTest Salts::Platform)
set_target_properties(yaml_fields_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
target_compile_definitions(yaml_fields_c_test PRIVATE
    PRAKTOR_YAML_FIELD_CORPUS_DIR="${CMAKE_CURRENT_LIST_DIR}/../../test/data")
add_test(NAME yaml_fields_c_test COMMAND yaml_fields_c_test)
add_executable(yaml_fields_fault_test ${CMAKE_CURRENT_LIST_DIR}/../../test/yaml_fields_fault_test.c)
target_include_directories(yaml_fields_fault_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(yaml_fields_fault_test PRIVATE Salts::YamlParser Salts::Core Salts::TinyTest)
set_target_properties(yaml_fields_fault_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME yaml_fields_fault_test COMMAND yaml_fields_fault_test)

# The fault provider compiles the same C implementation with boundary wrappers.
# Only application builds load C++; the independent C project stays C-only.
if(CMAKE_CXX_COMPILER_LOADED)
    add_library(yaml_document_fault_provider OBJECT ${CMAKE_CURRENT_LIST_DIR}/../../test/yaml_document_fault_test.c)
    target_compile_definitions(yaml_document_fault_provider PRIVATE PRAKTOR_YAML_FAULT_PROVIDER)
    target_include_directories(yaml_document_fault_provider PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../include)
    target_link_libraries(yaml_document_fault_provider PRIVATE Salts::YamlParser Salts::Core)
    set_target_properties(yaml_document_fault_provider PROPERTIES C_STANDARD 11
        C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    add_executable(yaml_document_legacy_fault_test
        ${CMAKE_CURRENT_LIST_DIR}/../../test/structured_document_query_test.cpp
        ${CMAKE_CURRENT_LIST_DIR}/yaml_fields.c
        ${CMAKE_CURRENT_LIST_DIR}/yaml_value.c
        ${CMAKE_CURRENT_LIST_DIR}/yaml_action_params.c
        ${CMAKE_CURRENT_LIST_DIR}/task_field_parser.cpp
        ${CMAKE_CURRENT_LIST_DIR}/yaml_document_legacy_bridge.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../data/workflow_value_legacy_bridge.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../data/structured_document_query_legacy_bridge.cpp
        $<TARGET_OBJECTS:yaml_document_fault_provider>)
    target_compile_definitions(yaml_document_legacy_fault_test PRIVATE PRAKTOR_YAML_FAULT_TEST)
    target_link_libraries(yaml_document_legacy_fault_test PRIVATE praktor_test_support
        praktor_document_query praktor_workflow_value Salts::YamlParser Salts::Core Salts::CSTL)
    target_include_directories(yaml_document_legacy_fault_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../../shared/process/include)
    target_compile_features(yaml_document_legacy_fault_test PRIVATE cxx_std_17)
    set_target_properties(yaml_document_legacy_fault_test PROPERTIES C_STANDARD 11
        C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    add_test(NAME yaml_document_legacy_fault_test COMMAND yaml_document_legacy_fault_test)
endif()
