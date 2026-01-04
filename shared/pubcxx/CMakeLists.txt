set(TARGET_NAME pubcxx)

file(GLOB_RECURSE PUBCXX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*
)

add_library(${TARGET_NAME} OBJECT ${PUBCXX_SOURCES})
# Includes Use standard target_include_directories for propagating include paths
target_include_directories(
    ${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Find dependencies
find_package(magic_enum CONFIG REQUIRED)
find_package(pegtl CONFIG REQUIRED)
find_package(mimalloc)
find_package(xxHash CONFIG REQUIRED)
# Link dependencies
target_link_libraries(
    ${TARGET_NAME}
    PUBLIC
    mimalloc
    hashmap
    xxHash::xxhash
    taocpp::pegtl
)
target_compile_definitions(
    ${TARGET_NAME} PUBLIC ACTIVE_LOG_LEVEL=LOG_LEVEL_PUBCXX_TRACE
)
set_target_properties(pubcxx PROPERTIES LINKER_LANGUAGE CXX)

set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "shared/${TARGET_NAME}")

add_subdirectory(example)
add_subdirectory(test)
