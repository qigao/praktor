find_package(Catch2 CONFIG REQUIRED)

set(TEST_SOURCES
    pistol_api_test.cpp
    template_contract_test.cpp
)

foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    set(executable_name "${test_name}.${TARGET_NAME_PISTOL}")
    add_executable(${executable_name} ${test_source})
    target_link_libraries(${executable_name} PRIVATE Catch2::Catch2WithMain ${TARGET_NAME_PISTOL} praktor_lib)
    add_test(NAME ${executable_name} COMMAND ${executable_name})
    set_target_properties(${executable_name} PROPERTIES FOLDER pistol/test)
endforeach()
