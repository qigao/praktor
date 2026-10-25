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
    src/workflow_plan.cpp
    src/workflow_contract.cpp
    src/workflow_effects.cpp
    src/data/workflow_value_legacy_bridge.cpp
    src/data/structured_document_query_legacy_bridge.cpp
    src/yml/yaml_document_legacy_bridge.cpp
    src/yml/yaml_contract_legacy_bridge.cpp
    src/yml/yaml_workflow_metadata_legacy_bridge.cpp
    src/yml/yaml_task_metadata_legacy_bridge.cpp
    src/yml/task_parser.cpp
    src/yml/orch_parser.cpp
    src/yml/task_field_parser.cpp
    src/yml/task_yaml.cpp
    src/yml/orch_legacy_bridge.cpp
    src/dag/workflow_executor.cpp
    src/dag/trigger_executor.cpp
    src/dag/task_executor_pool.cpp
    src/executors/dynamic_tasks_executor.cpp
    src/executors/uses_executor.cpp
    src/executors/declarative_tree_executor.cpp
    src/executors/program_executor.cpp
    src/executors/download_executor.cpp
    src/executors/service_executor.cpp
    src/executors/managed_process_executor.cpp
    src/executors/host_tool_executor.cpp
    src/system/service_control_legacy_bridge.cpp
    src/system/process_control_legacy_bridge.cpp
    src/system/process_native_legacy_bridge.cpp
    src/expressions/expression_evaluator_legacy_bridge.cpp
    src/util/system_info.cpp
    src/util/mustache_substitutor.cpp
)

if(ENABLE_SCRIPT_ENGINE)
    list(APPEND SOURCE_FILES src/script/script_engine.cpp)
else()
    list(APPEND SOURCE_FILES src/script/script_engine_disabled.cpp)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/src/expressions/lexer_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/expressions/evaluator_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/data/query_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/data/value_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/data/workflow_contract_value_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/yml/yaml_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/yml/yaml_value_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/yml/yaml_contract_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/yml/yaml_action_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/yml/yaml_task_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/yml/yaml_workflow_metadata_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/yml/yaml_task_metadata_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/yml/workflow_dependencies_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/yml/orch_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/yml/orch_yaml_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/profile/profile_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/util/file_hash_targets.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/src/util/praktor_init_targets.cmake)
add_library(${TARGET_NAME_LIB} ${SOURCE_FILES})
target_include_directories(
    ${TARGET_NAME_LIB}
    PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/generated/include
    PRIVATE src src/script
)

set(folder_name "${TARGET_NAME_EXE}")

target_link_libraries(
    ${TARGET_NAME_LIB}
    PUBLIC Threads::Threads
           Salts::Core
           Salts::JsonParser
           Salts::YamlParser
           Salts::CYamlJsonAdapter
           Salts::XmlParser
           Salts::CsvParser
           Salts::Mustache
           cxxopts::cxxopts
           CHttp::Client
           Salts::CNet
           CHttp::S3
           praktor_shell
    PRIVATE praktor_init praktor_file_hash praktor_orch_tree praktor_workflow_profile praktor_workflow_contract_value praktor_yaml_task_fields praktor_yaml_action_params praktor_yaml_contract praktor_yaml_workflow_metadata praktor_yaml_task_metadata praktor_yaml_value praktor_yaml_document praktor_workflow_value praktor_document_query praktor_expression_evaluator praktor_expression_regex_salts actions praktor_process_driver praktor_process_native praktor_service_control
)
if(ENABLE_SCRIPT_ENGINE)
    target_link_libraries(${TARGET_NAME_LIB} PUBLIC TurboScript::TurboScript)
endif()
# C11 remains opt-in; the Legacy runtime is a PRIVATE link dependency
# for the current implementation, not a public header/compile requirement.
# Do not remove its actual production link until full parity is accepted.
if(PRAKTOR_ENABLE_C11_ACTIONS)
    target_link_libraries(${TARGET_NAME_LIB} PRIVATE praktor_actions_c11)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(${TARGET_NAME_LIB} PUBLIC ${CMAKE_DL_LIBS})
endif()
target_compile_definitions(
    ${TARGET_NAME_LIB} PUBLIC PRAKTOR_SCRIPT_ENGINE_ENABLED=$<BOOL:${ENABLE_SCRIPT_ENGINE}>
                               PRAKTOR_C11_ACTIONS_ENABLED=$<BOOL:${PRAKTOR_ENABLE_C11_ACTIONS}>
                              $<$<BOOL:WIN32>:NOMINMAX>
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

cmake_config_target(
    ${TARGET_NAME_EXE}
    NO_INSTALL
    ALIAS Praktor::CLI
    EXPORT_NAME CLI
    FOLDER ${folder_name}
)

install(
    TARGETS ${TARGET_NAME_EXE}
    EXPORT PraktorTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(ENABLE_TESTS)
    add_subdirectory(test)
endif()
