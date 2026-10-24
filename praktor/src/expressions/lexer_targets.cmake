add_library(praktor_expression_lexer STATIC ${CMAKE_CURRENT_LIST_DIR}/expression_lexer.c)
target_include_directories(praktor_expression_lexer PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(praktor_expression_lexer PRIVATE Salts::Core)
target_compile_features(praktor_expression_lexer PUBLIC c_std_11)
set_target_properties(praktor_expression_lexer PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
