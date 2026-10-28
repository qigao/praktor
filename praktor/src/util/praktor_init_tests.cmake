add_executable(praktor_init_c_test ${CMAKE_CURRENT_LIST_DIR}/../../test/praktor_init_c_test.c)
target_link_libraries(praktor_init_c_test PRIVATE praktor_init Salts::TinyTest Salts::Platform)
set_target_properties(praktor_init_c_test PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_test(NAME praktor_init_c_test COMMAND praktor_init_c_test)
