set(TARGET_NAME_EXE weave)
set(TARGET_NAME_LIB ${TARGET_NAME_EXE}_lib)

set(SOURCE_FILES
    src/workflow_runner.cpp
    src/yml/task_parser.cpp
    src/yml/task_yaml.cpp
    src/dag/workflow_executor.cpp
    src/dag/task_executor_pool.cpp
    src/dag/executors/run_command_executor.cpp
    src/dag/executors/file_operation_executor.cpp
    src/dag/executors/control_flow_executor.cpp
    src/util/expression_evaluator.cpp
    src/util/template_engine.cpp
    src/util/system_info.cpp
 )

# Libary part
add_library(${TARGET_NAME_LIB} OBJECT ${SOURCE_FILES})
target_include_directories(${TARGET_NAME_LIB} PUBLIC include)

set(folder_name "${TARGET_NAME_EXE}")
find_package(ryml CONFIG REQUIRED)
find_package(Threads)
find_package(libuv)
find_package(jsoncons CONFIG REQUIRED)

# Link dependencies:
target_link_libraries(
    ${TARGET_NAME_LIB}
    PUBLIC
    Threads::Threads jsoncons::jsoncons
     $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    ryml::ryml
)
target_compile_definitions(${TARGET_NAME_LIB} PUBLIC $<$<BOOL:WIN32>:NOMINMAX>)

# link project_options/warnings
target_link_libraries(
    ${TARGET_NAME_LIB} PRIVATE
)

# Executable part
add_executable(${TARGET_NAME_EXE})
target_sources(${TARGET_NAME_EXE} PRIVATE main.cpp)
target_link_libraries(
    ${TARGET_NAME_EXE} PRIVATE ${TARGET_NAME_LIB}

)

set_target_properties(${TARGET_NAME_EXE} PROPERTIES FOLDER ${folder_name})
set_target_properties(${TARGET_NAME_LIB} PROPERTIES FOLDER ${folder_name})
if(ENABLE_TESTS)
    add_subdirectory(test)
endif()
