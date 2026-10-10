# Maintenance tool: cross builds consume the checked-in editor schema and never
# execute a target-architecture program on the build host.
if(CMAKE_CROSSCOMPILING)
    return()
endif()

add_library(praktor_schema_projection STATIC projection.cpp)
target_include_directories(praktor_schema_projection PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(praktor_schema_projection PUBLIC praktor_lib PRIVATE Salts::IDL Salts::Schema)
cmake_config_target(praktor_schema_projection NO_INSTALL NO_VERSION FOLDER tools/schema)

add_executable(praktor-schema main.cpp)
target_link_libraries(praktor-schema PRIVATE praktor_schema_projection)
cmake_config_target(praktor-schema NO_INSTALL NO_VERSION FOLDER tools/schema)

add_custom_target(update-workflow-schema
    COMMAND praktor-schema
        ${PROJECT_SOURCE_DIR}/schema/workflow.schema
        ${PROJECT_SOURCE_DIR}/schema/workflow.editor.json
        ${PROJECT_SOURCE_DIR}/grammar.schema.json
    DEPENDS praktor-schema
    COMMENT "Projecting the workflow editor schema from Salts IDL and Schema"
    VERBATIM
)

if(ENABLE_TESTS)
    cmake_add_test(
        SOURCES ${PROJECT_SOURCE_DIR}/praktor/test/schema_projection_test.cpp
        LIBS Catch2::Catch2WithMain praktor_schema_projection
        DEFS PRAKTOR_SCHEMA_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        FOLDER tools/schema/test
    )
endif()
