# C-only tests, scheduled after merge of the breaking runtime.
foreach(tests IN ITEMS
    ../src/expressions/lexer_tests.cmake
    ../src/expressions/evaluator_tests.cmake
    ../src/data/query_tests.cmake
    ../src/data/value_tests.cmake
    ../src/data/workflow_contract_value_tests.cmake
    ../src/yml/yaml_tests.cmake
    ../src/yml/yaml_value_tests.cmake
    ../src/yml/yaml_contract_tests.cmake
    ../src/yml/yaml_action_tests.cmake
    ../src/yml/yaml_task_tests.cmake
    ../src/yml/orch_yaml_tests.cmake
    ../src/profile/profile_tests.cmake
    ../src/util/file_hash_tests.cmake)
    include("${CMAKE_CURRENT_SOURCE_DIR}/${tests}")
endforeach()
add_executable(praktor_runtime_c_test praktor_runtime_test.c)
target_link_libraries(praktor_runtime_c_test PRIVATE praktor_runtime Salts::TinyTest)
set_target_properties(praktor_runtime_c_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME praktor_runtime_c_test COMMAND praktor_runtime_c_test)
set_tests_properties(praktor_runtime_c_test PROPERTIES TIMEOUT 45)
