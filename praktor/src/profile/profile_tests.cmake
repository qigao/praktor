add_executable(workflow_profile_c_test ${CMAKE_CURRENT_LIST_DIR}/../../test/workflow_profile_c_test.c)
target_link_libraries(workflow_profile_c_test PRIVATE praktor_workflow_profile Salts::TinyTest Salts::Platform)
set_target_properties(workflow_profile_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_profile_c_test COMMAND workflow_profile_c_test)
