include(${CMAKE_CURRENT_LIST_DIR}/session_core_targets.cmake)
add_library(praktor_c11_session STATIC
    ${CMAKE_CURRENT_LIST_DIR}/tree_plan.cpp
    ${CMAKE_CURRENT_LIST_DIR}/session_ast_bridge.cpp)
target_include_directories(praktor_c11_session PUBLIC
    ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(praktor_c11_session PUBLIC praktor_c11_session_core)
target_compile_features(praktor_c11_session PUBLIC cxx_std_17)
set_target_properties(praktor_c11_session PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
