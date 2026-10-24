add_executable(structured_document_query_c_test ${CMAKE_CURRENT_LIST_DIR}/../../test/structured_document_query_test.c)
target_link_libraries(structured_document_query_c_test PRIVATE praktor_document_query Salts::TinyTest)
set_target_properties(structured_document_query_c_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME structured_document_query_c_test COMMAND structured_document_query_c_test)
