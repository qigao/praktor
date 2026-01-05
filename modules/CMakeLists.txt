find_package(qjs REQUIRED)
find_package(libuv CONFIG REQUIRED)

message(STATUS "C implicit dirs: ${CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES}")
message(STATUS "CXX implicit dirs: ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES}")
message(
    STATUS "Windows SDK version: ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}"
)

set(TARGET_NAME quick)
file(GLOB_RECURSE WORK_FLOW_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h*
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

add_library(${TARGET_NAME} STATIC ${WORK_FLOW_SOURCES})
if(CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES)
    target_include_directories(
        ${TARGET_NAME} PRIVATE ${CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES}
    )
endif()
if(CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
    target_include_directories(
        ${TARGET_NAME} PRIVATE ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES}
    )
endif()
# Includes Use standard target_include_directories for propagating include paths
target_include_directories(
    ${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
# Default link options
target_link_libraries(${TARGET_NAME} PUBLIC)
# Find dependencies Link dependencies
target_link_libraries(${TARGET_NAME} PUBLIC libuv::uv qjs)
include_directories(${Boost_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR})
set_target_properties(${TARGET_NAME} PROPERTIES LINKER_LANGUAGE C)

set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "${TARGET_NAME}")
add_subdirectory(examples)
add_subdirectory(test)
