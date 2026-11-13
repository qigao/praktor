add_executable(expression_evaluator_c_test ${CMAKE_CURRENT_LIST_DIR}/expression_evaluator_test.c)
target_link_libraries(expression_evaluator_c_test PRIVATE praktor_expression_evaluator
    praktor_expression_regex_salts Salts::TinyTest Salts::Core)
set_target_properties(expression_evaluator_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME expression_evaluator_c_test COMMAND expression_evaluator_c_test)
