if(NOT TARGET praktor_process_native)
    # Persistent-application host + control state machine. No Salts::Process,
    # C++ compiler, Shell/Program runtime or Legacy execution dependency.
    add_library(praktor_process_native STATIC
        ${CMAKE_CURRENT_LIST_DIR}/src/process_control.c
        ${CMAKE_CURRENT_LIST_DIR}/src/process_native.c)
    target_include_directories(praktor_process_native PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
    target_link_libraries(praktor_process_native PRIVATE Salts::Platform)
    if(WIN32)
        target_link_libraries(praktor_process_native PRIVATE user32)
    endif()
    target_compile_features(praktor_process_native PUBLIC c_std_11)
    set_target_properties(praktor_process_native PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
endif()
