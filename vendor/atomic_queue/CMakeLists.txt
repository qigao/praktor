add_library(atomic_queue INTERFACE)
file(GLOB_RECURSE SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.h*)
target_include_directories(atomic_queue INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_sources(atomic_queue INTERFACE ${SOURCE_FILES})
