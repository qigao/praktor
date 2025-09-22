find_package(absl REQUIRED PATHS ${EXTERNAL_ROOT}/abseil)
find_package(catch2 CONFIG REQUIRED)

set(TEST_SOURCES
    main_application_test.cpp
    task_parser_test.cpp
    system_info_test.cpp
    workflow_context_test.cpp
    expression_evaluator_test.cpp
    workflow_runner_test.cpp
    thread_pool_test.cpp
    plugin_system_test.cpp

)

foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    set(executable_name "${test_name}.${TARGET_NAME_LIB}")
    add_executable(${executable_name} ${test_source})
    target_link_libraries(${executable_name} PRIVATE Catch2::Catch2WithMain ${TARGET_NAME_LIB} absl::base absl::strings)
    add_test(NAME ${executable_name} COMMAND ${executable_name})
    set(EXAMPLE_NAME ${executable_name})
    set_target_properties(${EXAMPLE_NAME} PROPERTIES FOLDER ${folder_name}/test)
endforeach()
