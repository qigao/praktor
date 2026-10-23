# Maintenance tool: cross builds consume the checked-in editor schema and never
# execute a target-architecture program on the build host.
if(CMAKE_CROSSCOMPILING)
    return()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/schema_targets.cmake)

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
    include(${CMAKE_CURRENT_LIST_DIR}/schema_tests.cmake)
endif()
