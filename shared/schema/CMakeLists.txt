# Maintenance tool: cross builds consume the checked-in editor schema and never
# execute a target-architecture program on the build host.
if(CMAKE_CROSSCOMPILING)
    return()
endif()

add_library(praktor_schema_projection STATIC ${CMAKE_CURRENT_LIST_DIR}/projection.c)
target_include_directories(praktor_schema_projection PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(praktor_schema_projection PUBLIC Salts::JsonParser PRIVATE Salts::IDL Salts::Schema Salts::Core)
target_compile_features(praktor_schema_projection PUBLIC c_std_11)
set_target_properties(praktor_schema_projection PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON FOLDER shared/schema)

add_executable(praktor-schema ${CMAKE_CURRENT_LIST_DIR}/main.c)
target_link_libraries(praktor-schema PRIVATE praktor_schema_projection Salts::Core)
set_target_properties(praktor-schema PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF LINKER_LANGUAGE C FOLDER shared/schema)

add_custom_target(update-workflow-schema
    COMMAND praktor-schema
        ${PROJECT_SOURCE_DIR}/schema/workflow.schema
        ${PROJECT_SOURCE_DIR}/schema/workflow.editor.json
        ${PROJECT_SOURCE_DIR}/grammar.schema.json
    DEPENDS praktor-schema
    COMMENT "Projecting the workflow editor schema from Salts IDL and Schema"
    VERBATIM
)

if(BUILD_TESTS)
    add_subdirectory(test)
endif()
