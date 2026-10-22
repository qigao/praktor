# Shared by the main build and the independent, legacy-free Session project.
add_library(praktor_c11_native_host STATIC
    ${CMAKE_CURRENT_LIST_DIR}/native_host.c)
target_include_directories(praktor_c11_native_host PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(praktor_c11_native_host PUBLIC praktor_c11_value_store praktor_c11_bt_core PRIVATE Salts::Platform)
target_compile_features(praktor_c11_native_host PUBLIC c_std_11)
if(UNIX)
    target_link_libraries(praktor_c11_native_host PRIVATE m)
endif()
set_target_properties(praktor_c11_native_host PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)

set(PRAKTOR_PROCESS_RETAINER ON)
include(${CMAKE_CURRENT_LIST_DIR}/../../shared/process/process_target.cmake)
add_library(praktor_c11_session STATIC
    ${CMAKE_CURRENT_LIST_DIR}/tree_plan.cpp
    ${CMAKE_CURRENT_LIST_DIR}/tree_executor_session.cpp)
target_include_directories(praktor_c11_session PUBLIC
    ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(praktor_c11_session PUBLIC praktor_c11_bt_core praktor_process_driver
    PRIVATE Salts::JsonParser)
target_compile_features(praktor_c11_session PUBLIC cxx_std_17)
set_target_properties(praktor_c11_session PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
