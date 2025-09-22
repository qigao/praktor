set(TARGET_NAME disruptor)
# Find all files recursively
file(GLOB_RECURSE SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*)
add_library(${TARGET_NAME} INTERFACE)
target_sources(${TARGET_NAME} INTERFACE ${SOURCE_FILES})
target_include_directories(${TARGET_NAME} INTERFACE include) # Includes


add_subdirectory(example)

# Tests
if(ENABLE_TESTS)
    add_subdirectory(test)
endif()
