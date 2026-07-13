set(TARGET_NAME_SHARED_SHELL praktor_shell)

add_library(${TARGET_NAME_SHARED_SHELL}
    src/shell_executor.cpp
    src/process_executor.cpp
)

if(WIN32)
    target_sources(${TARGET_NAME_SHARED_SHELL} PRIVATE
        src/shell_executor_win.cpp
        src/process_executor_win.cpp
    )
elseif(APPLE OR CMAKE_SYSTEM_NAME MATCHES ".*BSD")
    target_sources(${TARGET_NAME_SHARED_SHELL} PRIVATE
        src/shell_executor_bsd.cpp
        src/shell_executor_posix.cpp
        src/process_executor_posix.cpp
    )
elseif(UNIX)
    target_sources(${TARGET_NAME_SHARED_SHELL} PRIVATE
        src/shell_executor_linux.cpp
        src/shell_executor_posix.cpp
        src/process_executor_posix.cpp
    )
else()
    message(FATAL_ERROR "praktor_shell: unsupported platform")
endif()

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
