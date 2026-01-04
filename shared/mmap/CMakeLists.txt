set(TARGET_NAME mmap)
# Find all files recursively

file(GLOB_RECURSE SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*
     ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*
)

add_library(${TARGET_NAME} OBJECT)
target_sources(${TARGET_NAME} PRIVATE ${SOURCE_FILES})
target_include_directories(
    ${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
find_package(ZLIB REQUIRED)
# Find dependencies
find_package(fmt CONFIG REQUIRED)
# Link dependencies
target_link_libraries(
    ${TARGET_NAME} PUBLIC pubcxx
    ZLIB::ZLIB
)

target_link_libraries(
    ${TARGET_NAME} PUBLIC
)
set_target_properties(
    ${TARGET_NAME} PROPERTIES FOLDER "shared/${TARGET_NAME}"
)
set_target_properties(${TARGET_NAME} PROPERTIES LINKER_LANGUAGE CXX)
add_subdirectory(example)
add_subdirectory(test)
