add_library(praktor_orch_tree STATIC ${CMAKE_CURRENT_LIST_DIR}/orch_tree.c)
target_include_directories(praktor_orch_tree PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(praktor_orch_tree PUBLIC Salts::Core PRIVATE Salts::CSTL)
target_compile_features(praktor_orch_tree PUBLIC c_std_11)
set_target_properties(praktor_orch_tree PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
