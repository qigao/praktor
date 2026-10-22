set(TARGET_NAME_SHARED_SHELL praktor_shell)

if(NOT TARGET Salts::Process)
    message(FATAL_ERROR "Praktor Shell and Program require the C11 Salts::Process SDK")
endif()
include(${CMAKE_CURRENT_LIST_DIR}/../process/process_target.cmake)
add_library(${TARGET_NAME_SHARED_SHELL}
    src/shell_executor.cpp
    src/managed_process.cpp
)
target_link_libraries(${TARGET_NAME_SHARED_SHELL} PRIVATE praktor_process_driver)

target_include_directories(
    ${TARGET_NAME_SHARED_SHELL}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(${TARGET_NAME_SHARED_SHELL} PUBLIC cxx_std_17)

set_target_properties(${TARGET_NAME_SHARED_SHELL} PROPERTIES POSITION_INDEPENDENT_CODE ON)
cmake_config_target(${TARGET_NAME_SHARED_SHELL} NO_INSTALL NO_VERSION FOLDER "shared/")

if(ENABLE_TESTS)
    add_subdirectory(test)
endif()
