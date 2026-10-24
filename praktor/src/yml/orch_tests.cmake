add_executable(orch_tree_c_test ${CMAKE_CURRENT_LIST_DIR}/../../test/orch_tree_test.c)
target_link_libraries(orch_tree_c_test PRIVATE praktor_orch_tree Salts::TinyTest Salts::Platform)
set_target_properties(orch_tree_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME orch_tree_c_test COMMAND orch_tree_c_test)
add_executable(orch_tree_fault_test ${CMAKE_CURRENT_LIST_DIR}/../../test/orch_tree_fault_test.c)
target_include_directories(orch_tree_fault_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(orch_tree_fault_test PRIVATE Salts::CSTL Salts::Core Salts::TinyTest)
set_target_properties(orch_tree_fault_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME orch_tree_fault_test COMMAND orch_tree_fault_test)
