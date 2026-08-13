set(TARGET_NAME_EXE praktor)
set(TARGET_NAME_LIB ${TARGET_NAME_EXE}_lib)

# Generate version header from template in build directory
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/util/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/include/util/version.hpp @ONLY
)
# ── SOURCE FILES ──

set(SOURCE_FILES
    src/workflow_runner.cpp
    src/data/workflow_value.cpp
    src/data/structured_document_query.cpp
    src/yml/yaml_document.cpp
    src/yml/task_parser.cpp
    src/yml/orch_parser.cpp
    src/yml/task_field_parser.cpp
    src/yml/task_yaml.cpp
    src/yml/task_types.cpp
    src/dag/workflow_executor.cpp
    src/dag/trigger_executor.cpp
    src/dag/task_executor_pool.cpp
    src/executors/dynamic_tasks_executor.cpp
    src/executors/uses_executor.cpp
    src/executors/declarative_tree_executor.cpp
    src/executors/program_executor.cpp
    src/expressions/expression_evaluator.cpp
    src/expressions/expression_lexer.cpp
    src/util/system_info.cpp
    src/util/praktor_init.cpp
    src/util/file_hash.cpp
    src/util/mustache_substitutor.cpp
    # Script engine (TurboScript)
    src/script/script_engine.cpp
)

add_library(${TARGET_NAME_LIB} ${SOURCE_FILES})
target_include_directories(
    ${TARGET_NAME_LIB}
    PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/generated/include
    PRIVATE src/script
)

set(folder_name "${TARGET_NAME_EXE}")

target_link_libraries(
    ${TARGET_NAME_LIB}
    PUBLIC Threads::Threads
           TurboUtils::Core
           TurboParser::Parser
           TurboParser::Mustache
           cxxopts::cxxopts
           TurboScript::TurboScript
           actions
)
if(UNIX AND NOT APPLE)
    target_link_libraries(${TARGET_NAME_LIB} PUBLIC ${CMAKE_DL_LIBS})
endif()
target_compile_definitions(
    ${TARGET_NAME_LIB} PUBLIC $<$<BOOL:WIN32>:NOMINMAX>
                              $<$<BOOL:WIN32>:WIN32_LEAN_AND_MEAN>
)
# MSVC: exprtk_test.cpp is huge, needs /bigobj
if(MSVC)
    target_compile_options(${TARGET_NAME_LIB} PUBLIC /bigobj)
endif()

# Enable PIC so praktor_lib can be linked into the new praktor_api DLL
set_target_properties(
    ${TARGET_NAME_LIB} PROPERTIES POSITION_INDEPENDENT_CODE ON
)
cmake_config_target(
    ${TARGET_NAME_LIB} NO_INSTALL NO_VERSION FOLDER ${folder_name}
)

add_executable(${TARGET_NAME_EXE})
target_sources(${TARGET_NAME_EXE} PRIVATE main.cpp)
target_link_libraries(${TARGET_NAME_EXE} PRIVATE ${TARGET_NAME_LIB})

# Copy template files next to the runtime output so `praktor init` works from
# the built binary.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/templates
     DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# Keep a copy under the subproject build directory for local development
# workflows.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/templates
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
)

cmake_config_target(${TARGET_NAME_EXE} NO_INSTALL FOLDER ${folder_name})

install(
    TARGETS ${TARGET_NAME_EXE}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(ENABLE_TESTS)
    add_subdirectory(test)
endif()
