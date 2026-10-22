if(NOT TARGET praktor_process_driver)
    add_library(praktor_process_driver STATIC
        ${CMAKE_CURRENT_LIST_DIR}/src/process_driver.c)
    target_include_directories(praktor_process_driver PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include)
    target_link_libraries(praktor_process_driver PUBLIC Salts::Process PRIVATE Salts::Platform)
    if(PRAKTOR_ENABLE_C11_ACTIONS OR PRAKTOR_PROCESS_RETAINER)
        target_compile_definitions(praktor_process_driver PUBLIC PRAKTOR_PROCESS_RETAINER=1)
    endif()
    target_compile_features(praktor_process_driver PUBLIC c_std_11)
    set_target_properties(praktor_process_driver PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
endif()
