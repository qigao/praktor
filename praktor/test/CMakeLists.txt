find_package(catch2 CONFIG REQUIRED)

set(TEST_SOURCES
    dynamic_tasks_executor_test.cpp
    expression_evaluator_test.cpp
    graph_test.cpp
    system_info_test.cpp
    task_parser_test.cpp
    task_registry_test.cpp
    script_executor_test.cpp
    concurrency_test.cpp
    thread_pool_test.cpp
    trigger_parser_test.cpp
    workflow_context_test.cpp
    workflow_runner_test.cpp
    core_improvements_test.cpp
    matrix_test.cpp
)

# Copy test fixtures to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
     DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    set(executable_name "${test_name}.${TARGET_NAME_LIB}")
    add_executable(${executable_name} ${test_source})
    target_link_libraries(${executable_name} PRIVATE Catch2::Catch2WithMain ${TARGET_NAME_LIB})
    add_test(NAME ${executable_name} COMMAND ${executable_name})
    set_target_properties(${executable_name} PROPERTIES FOLDER ${folder_name}/test)
endforeach()
