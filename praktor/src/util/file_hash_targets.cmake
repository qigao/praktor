add_library(praktor_file_hash STATIC ${CMAKE_CURRENT_LIST_DIR}/file_hash.c)
target_include_directories(praktor_file_hash PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_compile_features(praktor_file_hash PUBLIC c_std_11)
set_target_properties(praktor_file_hash PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
