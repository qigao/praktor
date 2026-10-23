if(NOT TARGET praktor_output_core)
    add_library(praktor_output_core STATIC ${CMAKE_CURRENT_LIST_DIR}/core/output_scan.c)
    target_include_directories(praktor_output_core PUBLIC ${CMAKE_CURRENT_LIST_DIR})
    target_compile_features(praktor_output_core PUBLIC c_std_11)
    set_target_properties(praktor_output_core PROPERTIES C_STANDARD 11
        C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
endif()
