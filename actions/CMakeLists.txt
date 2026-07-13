cmake_minimum_required(VERSION 3.16)

project(
    actions
    VERSION 2.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

cmake_add_source(ALL_SOURCE_FILES RECURSE
    DIRS src core
    PATTERNS "*.cpp"
)
cmake_add_source(ALL_HEADER_FILES RECURSE
    DIRS include/actions core
    PATTERNS "*.hpp" "*.h"
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
set_target_properties(actions PROPERTIES POSITION_INDEPENDENT_CODE ON)
cmake_config_target(actions NO_INSTALL FOLDER "actions/" VERSION ${PROJECT_VERSION})

# ── DEPENDENCIES ──

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

if(ENABLE_TESTS)
    add_subdirectory(test)
endif()
