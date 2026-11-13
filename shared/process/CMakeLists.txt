add_library(praktor_process_retainer STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/process_retainer.c)
target_include_directories(praktor_process_retainer PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(praktor_process_retainer PUBLIC Salts::NativeIO)
target_compile_features(praktor_process_retainer PUBLIC c_std_11)
set_target_properties(praktor_process_retainer PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
add_library(praktor_process_driver STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/process_driver.c
    ${CMAKE_CURRENT_LIST_DIR}/src/process_worker.c)
target_include_directories(praktor_process_driver PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(praktor_process_driver PUBLIC praktor_process_retainer PRIVATE Salts::Core)
target_compile_features(praktor_process_driver PUBLIC c_std_11)
set_target_properties(praktor_process_driver PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
# Persistent-application host + control state machine. No Salts::NativeIO,
# C++ compiler, Shell/Program runtime or Legacy execution dependency.
add_library(praktor_process_native STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/process_control.c
    ${CMAKE_CURRENT_LIST_DIR}/src/process_native.c)
target_include_directories(praktor_process_native PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(praktor_process_native PRIVATE Salts::Core)
if(WIN32)
    target_link_libraries(praktor_process_native PRIVATE user32)
endif()
target_compile_features(praktor_process_native PUBLIC c_std_11)
set_target_properties(praktor_process_native PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
add_library(praktor_service_control STATIC ${CMAKE_CURRENT_LIST_DIR}/src/service_control.c)
target_include_directories(praktor_service_control PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(praktor_service_control PRIVATE Salts::Core)
set_target_properties(praktor_service_control PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
add_library(praktor_service_process_host STATIC ${CMAKE_CURRENT_LIST_DIR}/src/service_process_host.c)
target_include_directories(praktor_service_process_host PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(praktor_service_process_host PUBLIC praktor_service_control PRIVATE praktor_process_driver)
set_target_properties(praktor_service_process_host PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)

if(BUILD_TESTS)
    add_subdirectory(test)
endif()
