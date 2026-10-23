if(NOT TARGET praktor_service_control)
    add_library(praktor_service_control STATIC ${CMAKE_CURRENT_LIST_DIR}/src/service_control.c)
    target_include_directories(praktor_service_control PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
    target_link_libraries(praktor_service_control PRIVATE Salts::Platform)
    set_target_properties(praktor_service_control PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
endif()
if(TARGET praktor_process_driver AND NOT TARGET praktor_service_process_host)
    add_library(praktor_service_process_host STATIC ${CMAKE_CURRENT_LIST_DIR}/src/service_process_host.c)
    target_include_directories(praktor_service_process_host PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
    target_link_libraries(praktor_service_process_host PUBLIC praktor_service_control PRIVATE praktor_process_driver)
    set_target_properties(praktor_service_process_host PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
endif()
