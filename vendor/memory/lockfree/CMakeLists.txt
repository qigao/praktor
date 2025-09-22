# Project Name
set(TARGET_NAME lockfree)

# Find all files recursively
file(GLOB_RECURSE SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/lockfree/*.h*)
# Library configuration
if(DEFINED LOCKFREE_CACHE_COHERENT)
    target_compile_definitions(${TARGET_NAME} INTERFACE LOCKFREE_CACHE_COHERENT)
endif()

if(DEFINED LOCKFREE_CACHELINE_LENGTH)
    target_compile_definitions(
        ${TARGET_NAME}
        INTERFACE LOCKFREE_CACHELINE_LENGTH=${LOCKFREE_CACHELINE_LENGTH}
    )
endif()

add_library(${TARGET_NAME} INTERFACE)
target_include_directories(
    ${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/lockfree
) # Includes


# Tests
if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()
