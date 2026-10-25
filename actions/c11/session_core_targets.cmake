include(${CMAKE_CURRENT_LIST_DIR}/../output_targets.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/regex_tre_target.cmake)
# C-only Session and host targets; usable from a project with LANGUAGES C.
add_library(praktor_c11_native_host STATIC
    ${CMAKE_CURRENT_LIST_DIR}/native_host.c)
target_include_directories(praktor_c11_native_host PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(praktor_c11_native_host PUBLIC praktor_c11_value_store praktor_c11_bt_core PRIVATE Salts::Platform Salts::JsonParser praktor_output_core praktor_regex_tre)
target_compile_features(praktor_c11_native_host PUBLIC c_std_11)
if(UNIX)
    target_link_libraries(praktor_c11_native_host PRIVATE m)
endif()
set_target_properties(praktor_c11_native_host PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)

set(PRAKTOR_PROCESS_RETAINER ON)
include(${CMAKE_CURRENT_LIST_DIR}/../../shared/process/process_target.cmake)
add_library(praktor_c11_session_core STATIC
    ${CMAKE_CURRENT_LIST_DIR}/plan.c
    ${CMAKE_CURRENT_LIST_DIR}/session.c
    ${CMAKE_CURRENT_LIST_DIR}/session_values.c
    ${CMAKE_CURRENT_LIST_DIR}/session_controls.c)
target_include_directories(praktor_c11_session_core PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(praktor_c11_session_core PUBLIC praktor_c11_bt_core praktor_process_driver PRIVATE Salts::JsonParser)
target_compile_features(praktor_c11_session_core PUBLIC c_std_11)
if(UNIX)
    target_link_libraries(praktor_c11_session_core PRIVATE m)
endif()
set_target_properties(praktor_c11_session_core PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
