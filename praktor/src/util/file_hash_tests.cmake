add_executable(praktor_file_hash_c_test ${CMAKE_CURRENT_LIST_DIR}/../../test/file_hash_c_test.c)
target_link_libraries(praktor_file_hash_c_test PRIVATE praktor_file_hash Salts::TinyTest Salts::Platform)
set_target_properties(praktor_file_hash_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME praktor_file_hash_c_test COMMAND praktor_file_hash_c_test)
