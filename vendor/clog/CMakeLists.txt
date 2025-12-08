# Unity Testing Framework CMake Configuration

# Create Unity static library
add_library(clog STATIC
    src/log.c
)

# Set include directories
target_include_directories(clog
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Unity target properties
set_target_properties(clog PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# Compiler flags (Unity uses C99 features)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(clog PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-unused-variable
    )
elseif(MSVC)
    target_compile_options(clog PRIVATE
        /W4
        /wd4100  # Suppress unused parameter warnings
        /wd4127  # Suppress conditional expression is constant warnings
    )
endif()
add_compile_definitions(clog LOG_USE_COLOR)
set_target_properties(clog PROPERTIES FOLDER "vendor/clog")
 