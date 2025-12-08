set(TARGET_NAME arena)
add_library(${TARGET_NAME} STATIC
    src/arena.c include/arena.h
)

# Set include directories
target_include_directories(${TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
 
)

# ${TARGET_NAME} target properties
set_target_properties(${TARGET_NAME} PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# Compiler flags (${TARGET_NAME} uses C99 features)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${TARGET_NAME} PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-unused-variable
    )
elseif(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE
        /W4
        /wd4100  # Suppress unused parameter warnings
        /wd4127  # Suppress conditional expression is constant warnings
    )
endif()
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "vendor/arena")
