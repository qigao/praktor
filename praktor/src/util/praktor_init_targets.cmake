add_library(praktor_init STATIC ${CMAKE_CURRENT_LIST_DIR}/praktor_init.c)
target_include_directories(praktor_init PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(praktor_init PUBLIC Salts::Core PRIVATE Salts::CSTL)
target_compile_features(praktor_init PUBLIC c_std_11)
set_target_properties(praktor_init PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
