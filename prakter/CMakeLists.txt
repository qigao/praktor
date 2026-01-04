set(TARGET_NAME_EXE prakter)
set(TARGET_NAME_LIB ${TARGET_NAME_EXE}_lib)

# Generate version header from template in build directory
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/util/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/include/util/version.hpp
    @ONLY
)

set(SOURCE_FILES
    src/workflow_runner.cpp
    src/yml/task_parser.cpp
    src/yml/task_yaml.cpp
    src/dag/workflow_executor.cpp
    src/dag/trigger_executor.cpp
    src/dag/task_executor_pool.cpp
    src/executors/command_executor.cpp
    src/executors/dynamic_tasks_executor.cpp
    src/executors/script_executor.cpp
    src/executors/uses_executor.cpp
    src/expressions/expression_evaluator.cpp
    src/util/expression_evaluator.cpp
    src/util/system_info.cpp
    src/util/prakter_init.cpp
    src/util/file_hash.cpp
 )

add_library(${TARGET_NAME_LIB}  ${SOURCE_FILES})
target_include_directories(${TARGET_NAME_LIB} PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/generated/include)

set(folder_name "${TARGET_NAME_EXE}")
find_package(ryml CONFIG REQUIRED)
find_package(Threads)
find_package(libuv)
find_package(jsoncons CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)

target_link_libraries(
    ${TARGET_NAME_LIB}
    PUBLIC
    Threads::Threads jsoncons::jsoncons
     $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    ryml::ryml
    quick cxxopts::cxxopts
    pubcxx
    fmtlog
)
target_compile_definitions(${TARGET_NAME_LIB} PUBLIC $<$<BOOL:WIN32>:NOMINMAX>)

add_executable(${TARGET_NAME_EXE})
target_sources(${TARGET_NAME_EXE} PRIVATE main.cpp)
target_link_libraries(
    ${TARGET_NAME_EXE} PRIVATE ${TARGET_NAME_LIB}
)

# Copy template files to the build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/templates DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

set_target_properties(${TARGET_NAME_EXE} PROPERTIES FOLDER ${folder_name})
set_target_properties(${TARGET_NAME_LIB} PROPERTIES FOLDER ${folder_name})
if(ENABLE_TESTS)
    add_subdirectory(test)
endif()
