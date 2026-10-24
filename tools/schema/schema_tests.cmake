get_filename_component(_schema_repo "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(_schema_generated "${CMAKE_CURRENT_BINARY_DIR}/workflow.generated.json")
add_test(NAME schema_projection_cli COMMAND praktor-schema
    "${_schema_repo}/schema/workflow.schema" "${_schema_repo}/schema/workflow.editor.json" "${_schema_generated}")
set_tests_properties(schema_projection_cli PROPERTIES FIXTURES_SETUP schema_projection_cli)
add_executable(schema_projection_test ${CMAKE_CURRENT_LIST_DIR}/test/projection_test.c)
target_link_libraries(schema_projection_test PRIVATE praktor_schema_projection Salts::TinyTest Salts::Core)
target_compile_definitions(schema_projection_test PRIVATE
    PRAKTOR_SCHEMA_SOURCE_DIR="${_schema_repo}" PRAKTOR_SCHEMA_GENERATED_PATH="${_schema_generated}")
set_target_properties(schema_projection_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME schema_projection_test COMMAND schema_projection_test)
set_tests_properties(schema_projection_test PROPERTIES FIXTURES_REQUIRED schema_projection_cli)
add_test(NAME schema_projection_cli_usage COMMAND praktor-schema)
set_tests_properties(schema_projection_cli_usage PROPERTIES WILL_FAIL TRUE)
