find_package(catch2 CONFIG REQUIRED)

set(TEST_SOURCES
    task_parser_test.cpp
    workflow_runner_test.cpp
)

foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    set(executable_name "${test_name}.${TARGET_NAME_LIB}")
    add_executable(${executable_name} ${test_source})
    target_link_libraries(${executable_name} PRIVATE Catch2::Catch2WithMain ${TARGET_NAME_LIB})
    add_test(NAME ${executable_name} COMMAND ${executable_name})
    set_target_properties(${executable_name} PROPERTIES FOLDER ${folder_name}/test)
endforeach()
