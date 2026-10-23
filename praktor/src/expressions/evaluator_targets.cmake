add_library(praktor_expression_evaluator STATIC ${CMAKE_CURRENT_LIST_DIR}/expression_evaluator.c)
target_include_directories(praktor_expression_evaluator PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(praktor_expression_evaluator PUBLIC Salts::Core PRIVATE praktor_expression_lexer)
if(UNIX)
    target_link_libraries(praktor_expression_evaluator PRIVATE m)
endif()
target_compile_features(praktor_expression_evaluator PUBLIC c_std_11)
set_target_properties(praktor_expression_evaluator PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
