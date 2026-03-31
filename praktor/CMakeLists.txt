set(TARGET_NAME_EXE praktor)
set(TARGET_NAME_LIB ${TARGET_NAME_EXE}_lib)

# Generate version header from template in build directory
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/util/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/include/util/version.hpp @ONLY
)
find_package(TurboNet CONFIG REQUIRED PATHS ${EXTERNAL_ROOT}/turbonet)

# ── SOURCE FILES ──

set(SOURCE_FILES
    src/workflow_runner.cpp
    src/yml/task_parser.cpp
    src/yml/task_yaml.cpp
    src/yml/task_types.cpp
    src/dag/workflow_executor.cpp
    src/dag/trigger_executor.cpp
    src/dag/task_executor_pool.cpp
    src/executors/dynamic_tasks_executor.cpp
    src/executors/uses_executor.cpp
    src/executors/declarative_tree_executor.cpp
    src/expressions/expression_evaluator.cpp
    src/expressions/expression_lexer.cpp
    src/util/system_info.cpp
    src/util/praktor_init.cpp
    src/util/file_hash.cpp
    src/util/mustache_substitutor.cpp
    src/util/native_loader.cpp
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
find_package(ryml CONFIG REQUIRED)
find_package(Threads)
find_package(jsoncons CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)
find_package(TurboNet CONFIG REQUIRED PATHS ${EXTERNAL_ROOT}/turbonet)

file(GLOB PRAKTOR_SCRIPT_PLUGIN_DLLS "${EXTERNAL_ROOT}/turbonet/bin/*_plugin.dll")

function(praktor_copy_runtime_artifacts target_name)
    if(NOT WIN32)
        return()
    endif()

    # Command for runtime DLL dependencies
    # We include $<TARGET_FILE:${target_name}> as an "anchor" source to prevent
    # copy_if_different from failing if the DLL list is empty.
    add_custom_command(
        TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${target_name}>
                $<TARGET_RUNTIME_DLLS:${target_name}>
                $<TARGET_FILE_DIR:${target_name}>
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    # Command for script plugins (only if they exist)
    if(PRAKTOR_SCRIPT_PLUGIN_DLLS)
        add_custom_command(
            TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${PRAKTOR_SCRIPT_PLUGIN_DLLS}
                    $<TARGET_FILE_DIR:${target_name}>
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    endif()
endfunction()

target_link_libraries(
    ${TARGET_NAME_LIB}
    PUBLIC Threads::Threads
           jsoncons::jsoncons
           OpenSSL::SSL
           OpenSSL::Crypto
           ryml::ryml
           TurboNet::Utils
           TurboNet::Mustache
           TurboNet::HttpClient
           cxxopts::cxxopts
           TurboScript::ExprTk
           TurboScript::TurboScript
           btdsl
)
target_compile_definitions(
    ${TARGET_NAME_LIB} PUBLIC $<$<BOOL:WIN32>:NOMINMAX>
                              $<$<BOOL:WIN32>:WIN32_LEAN_AND_MEAN>
)
# MSVC: exprtk_test.cpp is huge, needs /bigobj
if(MSVC)
    target_compile_options(${TARGET_NAME_LIB} PUBLIC /bigobj)
endif()

# Enable PIC so praktor_lib can be linked into the new praktor_api DLL
set_target_properties(${TARGET_NAME_LIB} PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(${TARGET_NAME_EXE})
target_sources(${TARGET_NAME_EXE} PRIVATE main.cpp)
target_link_libraries(${TARGET_NAME_EXE} PRIVATE ${TARGET_NAME_LIB})

# Copy template files next to the runtime output so `praktor init` works from the built binary.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/templates
     DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# Keep script plugins beside built executables so TurboScript import("feeds") works on Windows.
if(WIN32 AND PRAKTOR_SCRIPT_PLUGIN_DLLS)
    file(COPY ${PRAKTOR_SCRIPT_PLUGIN_DLLS}
         DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    )
endif()

# Keep a copy under the subproject build directory for local development workflows.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/templates
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
)

praktor_copy_runtime_artifacts(${TARGET_NAME_EXE})

set_target_properties(${TARGET_NAME_EXE} PROPERTIES FOLDER ${folder_name})
set_target_properties(${TARGET_NAME_LIB} PROPERTIES FOLDER ${folder_name})
if(ENABLE_TESTS)
    add_subdirectory(test)
endif()
