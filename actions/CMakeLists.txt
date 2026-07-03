cmake_minimum_required(VERSION 3.16)

project(
    actions
    VERSION 2.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── COLLECT ALL SOURCE FILES ──

file(GLOB_RECURSE ALL_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp
)

file(
    GLOB_RECURSE
    ALL_HEADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/actions/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/actions/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/core/*.hpp
)
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "praktor_integration\\.cpp$")
list(FILTER ALL_HEADER_FILES EXCLUDE REGEX "praktor_integration\\.hpp$")

# ── MAIN LIBRARY ──

add_library(actions ${ALL_SOURCE_FILES} ${ALL_HEADER_FILES})

target_include_directories(
    actions
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
           $<INSTALL_INTERFACE:include>
)

target_compile_features(actions PUBLIC cxx_std_17)
set_target_properties(
    actions PROPERTIES FOLDER "actions/" VERSION ${PROJECT_VERSION}
                       POSITION_INDEPENDENT_CODE ON
)

# ── DEPENDENCIES ──

find_package(jsoncons CONFIG REQUIRED)
target_link_libraries(actions PUBLIC praktor_shell PRIVATE jsoncons::jsoncons)

if(WIN32)
    target_link_libraries(actions PRIVATE psapi)
endif()

# ── EXAMPLES ──

add_subdirectory(examples)

# ── INSTALL ──

install(
    TARGETS actions
    EXPORT actionsTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES
    DESTINATION include
)

install(DIRECTORY include/actions DESTINATION include)

install(
    DIRECTORY core/
    DESTINATION include/actions/core
    FILES_MATCHING
    PATTERN "*.hpp"
)

# ── TESTS ──

enable_testing()
add_subdirectory(test)
