add_executable(expression_lexer_test ${CMAKE_CURRENT_LIST_DIR}/../../test/expression_lexer_test.c)
target_link_libraries(expression_lexer_test PRIVATE praktor_expression_lexer Salts::TinyTest)
set_target_properties(expression_lexer_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME expression_lexer_test COMMAND expression_lexer_test)
