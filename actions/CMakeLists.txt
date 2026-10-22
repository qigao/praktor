cmake_minimum_required(VERSION 3.16)

project(
    actions
    VERSION 2.0.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PRAKTOR_ENABLE_C11_ACTIONS
       "Build the experimental Salts C11 action-tree adapter and its tests"
       OFF)

cmake_add_source(ALL_SOURCE_FILES RECURSE
    DIRS src core
    PATTERNS "*.cpp"
)
cmake_add_source(ALL_HEADER_FILES RECURSE
    DIRS include/actions core
    PATTERNS "*.hpp" "*.h"
)
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "praktor_integration\\.cpp$")
list(FILTER ALL_HEADER_FILES EXCLUDE REGEX "praktor_integration\\.hpp$")

# ── MAIN LIBRARY ──

add_library(actions ${ALL_SOURCE_FILES} ${ALL_HEADER_FILES})

target_include_directories(
    actions
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
           $<INSTALL_INTERFACE:include>
)

target_compile_features(actions PUBLIC cxx_std_17)
target_compile_definitions(actions PRIVATE $<$<BOOL:WIN32>:NOMINMAX>)
set_target_properties(actions PROPERTIES POSITION_INDEPENDENT_CODE ON)
cmake_config_target(actions NO_INSTALL FOLDER "actions/" VERSION ${PROJECT_VERSION})

# ── DEPENDENCIES ──

target_link_libraries(actions PUBLIC praktor_shell PRIVATE Salts::JsonParser)

if(WIN32)
    target_link_libraries(actions PRIVATE psapi)
endif()

# This target is deliberately opt-in until the Praktor parity corpus passes.
# Enabling it is fail-fast: it never silently falls back when the selected
# SaltsUtils SDK does not export Salts::BehaviorTree.
if(PRAKTOR_ENABLE_C11_ACTIONS)
    if(NOT TARGET Salts::BehaviorTree)
        message(FATAL_ERROR
            "PRAKTOR_ENABLE_C11_ACTIONS requires an installed SaltsUtils SDK exporting Salts::BehaviorTree")
    endif()
    if(NOT TARGET Salts::Process)
        message(FATAL_ERROR
            "PRAKTOR_ENABLE_C11_ACTIONS requires Salts::Process; Shell execution never falls back to the legacy ShellExecutor")
    endif()

    # Native C11 adapter: no C++ actions::Executor or Shell dependency.
    add_library(praktor_c11_bt_core STATIC
        c11/tree_adapter.c
        c11/tree_adapter.h
    )
    target_include_directories(praktor_c11_bt_core
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/c11>
    )
    target_link_libraries(praktor_c11_bt_core PUBLIC Salts::BehaviorTree)
    target_compile_features(praktor_c11_bt_core PUBLIC c_std_11)
    set_target_properties(praktor_c11_bt_core PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
        FOLDER "actions/c11"
    )
    cmake_config_target(praktor_c11_bt_core NO_INSTALL NO_VERSION FOLDER "actions/c11")

    # Independent bounded C11 key/value storage; no BehaviorTree or actions dependency.
    add_library(praktor_c11_value_store STATIC c11/value_store.c c11/value_store.h)
    target_include_directories(praktor_c11_value_store PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/c11>)
    target_compile_features(praktor_c11_value_store PUBLIC c_std_11)
    set_target_properties(praktor_c11_value_store PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON FOLDER "actions/c11")
    cmake_config_target(praktor_c11_value_store NO_INSTALL NO_VERSION FOLDER "actions/c11")

    # Transitional C++ Workflow/AST bridge; remove after semantic parity.
    add_library(praktor_actions_c11 STATIC
        c11/tree_plan.cpp
        c11/tree_plan.hpp
        c11/tree_executor_session.cpp
        c11/tree_executor_session.hpp
    )
    target_include_directories(praktor_actions_c11
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/c11>
        PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    )
    target_link_libraries(praktor_actions_c11
        PUBLIC praktor_c11_bt_core Salts::Process actions
        PRIVATE Salts::JsonParser)
    target_compile_features(praktor_actions_c11 PRIVATE cxx_std_17)
    set_target_properties(praktor_actions_c11 PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
        FOLDER "actions/c11"
    )
    cmake_config_target(praktor_actions_c11 NO_INSTALL NO_VERSION FOLDER "actions/c11")

    if(ENABLE_TESTS)
        add_executable(actions_c11_tree_adapter_test c11/tree_adapter_test.c)
        target_link_libraries(actions_c11_tree_adapter_test PRIVATE praktor_c11_bt_core)
        target_compile_features(actions_c11_tree_adapter_test PRIVATE c_std_11)
        set_target_properties(actions_c11_tree_adapter_test PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
            FOLDER "actions/c11/tests"
        )
        add_test(NAME actions_c11_tree_adapter_test COMMAND actions_c11_tree_adapter_test)
        add_executable(actions_c11_value_store_test c11/value_store_test.c)
        target_link_libraries(actions_c11_value_store_test PRIVATE praktor_c11_value_store)
        target_compile_features(actions_c11_value_store_test PRIVATE c_std_11)
        set_target_properties(actions_c11_value_store_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF FOLDER "actions/c11/tests")
        add_test(NAME actions_c11_value_store_test COMMAND actions_c11_value_store_test)

        # Header contract: compile the C11 session API with Salts/AST
        # model headers only, without linking or including the legacy Executor.
        add_library(actions_c11_session_header_contract OBJECT
            c11/session_header_contract.cpp)
        target_include_directories(actions_c11_session_header_contract PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(actions_c11_session_header_contract PRIVATE
            praktor_c11_bt_core Salts::Process)
        target_compile_features(actions_c11_session_header_contract PRIVATE cxx_std_17)
        set_target_properties(actions_c11_session_header_contract PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
            FOLDER "actions/c11/tests"
        )

        add_executable(actions_c11_tree_plan_test c11/tree_plan_test.cpp)
        target_link_libraries(actions_c11_tree_plan_test PRIVATE praktor_actions_c11 actions)
        target_compile_features(actions_c11_tree_plan_test PRIVATE cxx_std_17)
        set_target_properties(actions_c11_tree_plan_test PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
            FOLDER "actions/c11/tests"
        )
        add_test(NAME actions_c11_tree_plan_test COMMAND actions_c11_tree_plan_test)

        add_executable(actions_c11_tree_executor_session_test c11/tree_executor_session_test.cpp)
        target_link_libraries(actions_c11_tree_executor_session_test PRIVATE praktor_actions_c11)
        target_compile_features(actions_c11_tree_executor_session_test PRIVATE cxx_std_17)
        set_target_properties(actions_c11_tree_executor_session_test PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
            FOLDER "actions/c11/tests"
        )
        add_test(NAME actions_c11_tree_executor_session_test COMMAND actions_c11_tree_executor_session_test)
    endif()
endif()

# ── EXAMPLES ──

add_subdirectory(examples)

# ── TESTS ──

if(ENABLE_TESTS)
    add_subdirectory(test)
endif()
