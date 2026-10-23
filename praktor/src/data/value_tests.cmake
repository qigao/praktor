add_executable(workflow_value_c_test ${CMAKE_CURRENT_LIST_DIR}/../../test/workflow_value_test.c)
target_link_libraries(workflow_value_c_test PRIVATE praktor_workflow_value Salts::TinyTest Salts::Platform)
set_target_properties(workflow_value_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_value_c_test COMMAND workflow_value_c_test)
add_executable(workflow_value_fault_test ${CMAKE_CURRENT_LIST_DIR}/../../test/workflow_value_fault_test.c)
target_link_libraries(workflow_value_fault_test PRIVATE Salts::JsonParser Salts::Core Salts::TinyTest)
target_include_directories(workflow_value_fault_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../include)
if(UNIX)
    target_link_libraries(workflow_value_fault_test PRIVATE m)
endif()
set_target_properties(workflow_value_fault_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME workflow_value_fault_test COMMAND workflow_value_fault_test)
