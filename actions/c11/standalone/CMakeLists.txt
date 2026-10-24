cmake_minimum_required(VERSION 3.27)
project(PraktorC11TreeAdapterStandalone LANGUAGES C)

include(CTest)

# Separate from Praktor's C++ root build and production link closure.
if(NOT IS_DIRECTORY "$ENV{SALTS_UTILS_ROOT}")
    message(FATAL_ERROR "SALTS_UTILS_ROOT must name the installed SaltsUtils SDK")
endif()
unset(SaltsUtils_DIR CACHE)
find_package(SaltsUtils CONFIG REQUIRED
    PATHS "$ENV{SALTS_UTILS_ROOT}/lib/cmake/SaltsUtils"
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
if(NOT TARGET Salts::BehaviorTree)
    message(FATAL_ERROR "installed SaltsUtils SDK is missing Salts::BehaviorTree")
endif()
if(CMAKE_CXX_COMPILER_LOADED OR TARGET actions OR TARGET praktor_actions_c11)
    message(FATAL_ERROR "C11 standalone adapter must not load C++ actions")
endif()

add_library(praktor_c11_bt_core STATIC
    ../tree_adapter.c
    ../tree_adapter.h
)
target_include_directories(praktor_c11_bt_core PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/.."
)
target_link_libraries(praktor_c11_bt_core PUBLIC Salts::BehaviorTree)
target_compile_features(praktor_c11_bt_core PUBLIC c_std_11)
set_target_properties(praktor_c11_bt_core PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
add_executable(actions_c11_tree_adapter_standalone_test
    ../tree_adapter_test.c
)
target_link_libraries(actions_c11_tree_adapter_standalone_test
    PRIVATE praktor_c11_bt_core
)
target_compile_features(actions_c11_tree_adapter_standalone_test PRIVATE c_std_11)
set_target_properties(actions_c11_tree_adapter_standalone_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    LINKER_LANGUAGE C
)
add_test(NAME actions_c11_tree_adapter_standalone_test
    COMMAND actions_c11_tree_adapter_standalone_test
)
add_executable(actions_c11_value_store_standalone_test ../value_store_test.c)
target_compile_features(actions_c11_value_store_standalone_test PRIVATE c_std_11)
set_target_properties(actions_c11_value_store_standalone_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE C)
add_library(praktor_c11_value_store STATIC ../value_store.c ../value_store.h)
target_include_directories(praktor_c11_value_store PUBLIC "${CMAKE_CURRENT_LIST_DIR}/..")
target_compile_features(praktor_c11_value_store PUBLIC c_std_11)
set_target_properties(praktor_c11_value_store PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(actions_c11_value_store_standalone_test PRIVATE praktor_c11_value_store Salts::TinyTest)
add_test(NAME actions_c11_value_store_standalone_test COMMAND actions_c11_value_store_standalone_test)

include(${CMAKE_CURRENT_LIST_DIR}/../../output_targets.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../regex_tre_target.cmake)
add_library(praktor_c11_native_host STATIC ../native_host.c)
target_include_directories(praktor_c11_native_host PUBLIC ..)
target_link_libraries(praktor_c11_native_host PUBLIC praktor_c11_value_store praktor_c11_bt_core PRIVATE Salts::Platform praktor_output_core praktor_regex_tre)
target_compile_features(praktor_c11_native_host PUBLIC c_std_11)
set_target_properties(praktor_c11_native_host PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(UNIX)
    target_link_libraries(praktor_c11_native_host PRIVATE m)
endif()
add_executable(actions_c11_native_host_standalone_test ../native_host_test.c)
target_link_libraries(actions_c11_native_host_standalone_test PRIVATE praktor_c11_native_host Salts::TinyTest)
set_target_properties(actions_c11_native_host_standalone_test PROPERTIES LINKER_LANGUAGE C)
add_test(NAME actions_c11_native_host_standalone_test COMMAND actions_c11_native_host_standalone_test)
add_executable(actions_c11_regex_tre_standalone_test ../regex_tre_test.c)
target_link_libraries(actions_c11_regex_tre_standalone_test PRIVATE praktor_regex_tre Salts::TinyTest)
set_target_properties(actions_c11_regex_tre_standalone_test PROPERTIES LINKER_LANGUAGE C)
add_test(NAME actions_c11_regex_tre_standalone_test COMMAND actions_c11_regex_tre_standalone_test)
