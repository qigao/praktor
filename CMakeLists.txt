cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 OLD)
endif()
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD) # for boost
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)

# Set additional project information
set(CMAKE_PROJECT_VERSION_MAJOR 0)
set(CMAKE_PROJECT_VERSION_MINOR 3)
set(CMAKE_PROJECT_VERSION_PATCH 0)

set(PROJECT_VERSION
    ${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}.${CMAKE_PROJECT_VERSION_PATCH}
)

set(PROJECT_NAME "Praktor")
set(PROJECT_DESCRIPTION "A task & workflow orchestrator")
set(PROJECT_WEBSITE "http://github.com/qigao/project-cpp-template.git")
set(APPLICATION_LICENSE "Apache2 License")

# Set the project name and language
project(
    ${PROJECT_NAME}
    VERSION ${PROJECT_VERSION}
    DESCRIPTION ${PROJECT_DESCRIPTION}
    HOMEPAGE_URL ${PROJECT_WEBSITE}
    LANGUAGES CXX C
)
# Cmake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(ProjectOptions.cmake)
include(CmakeUtils)
add_subdirectory(tools)
include(FindTools)

if(ENABLE_TESTS)
    enable_testing()
endif()

# Output files directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_EXECUTABLE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")


find_package(TurboUtils CONFIG REQUIRED PATHS ${TURBO_UTILS_ROOT})
find_package(TurboScript CONFIG REQUIRED PATHS "${TURBOSCRIPT_ROOT}")
find_package(cxxopts CONFIG REQUIRED)
if(ENABLE_TESTS)
    find_package(Catch2 CONFIG REQUIRED)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
endif()
add_subdirectory(shared)
add_subdirectory(praktor)
add_subdirectory(actions)
add_subdirectory(pistol)

set(PRAKTOR_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/Praktor")

install(
    EXPORT PraktorTargets
    FILE PraktorTargets.cmake
    NAMESPACE Praktor::
    DESTINATION "${PRAKTOR_INSTALL_CMAKEDIR}"
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PraktorConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PraktorConfig.cmake"
    INSTALL_DESTINATION "${PRAKTOR_INSTALL_CMAKEDIR}"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PraktorConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/PraktorConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/PraktorConfigVersion.cmake"
    DESTINATION "${PRAKTOR_INSTALL_CMAKEDIR}"
)
