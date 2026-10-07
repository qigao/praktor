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
set(CMAKE_PROJECT_VERSION_MINOR 4)
set(CMAKE_PROJECT_VERSION_PATCH 5)

set(PROJECT_VERSION
    ${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}.${CMAKE_PROJECT_VERSION_PATCH}
)

set(PROJECT_NAME "Praktor")
set(PROJECT_DESCRIPTION "A YAML workflow runtime for automation and AI agents")
set(PROJECT_WEBSITE "https://github.com/qigao/praktor")
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

set(_praktor_dependencies Salts SaltsUtils Chttp)
set(_praktor_dependency_roots SALTS_ROOT SALTS_UTILS_ROOT CHTTP_ROOT)
if(ENABLE_SCRIPT_ENGINE)
    list(APPEND _praktor_dependencies TurboScript)
    list(APPEND _praktor_dependency_roots TURBOSCRIPT_ROOT)
endif()
foreach(_root IN LISTS _praktor_dependency_roots)
    if(NOT DEFINED ENV{${_root}} OR NOT IS_DIRECTORY "$ENV{${_root}}")
        message(FATAL_ERROR "${_root} must name the matching installed SDK")
    endif()
    file(TO_CMAKE_PATH "$ENV{${_root}}" _praktor_${_root})
endforeach()
# A previous SDK's cached location must not override the selected profile.
foreach(_package IN LISTS _praktor_dependencies)
    unset(${_package}_DIR CACHE)
    unset(${_package}_DIR)
endforeach()
find_package(Salts CONFIG REQUIRED
             PATHS "${_praktor_SALTS_ROOT}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
find_package(SaltsUtils CONFIG REQUIRED
             PATHS "${_praktor_SALTS_UTILS_ROOT}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
find_package(Chttp CONFIG REQUIRED
             PATHS "${_praktor_CHTTP_ROOT}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
if(ENABLE_SCRIPT_ENGINE)
    find_package(TurboScript CONFIG REQUIRED
                 PATHS "${_praktor_TURBOSCRIPT_ROOT}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
endif()
find_package(cxxopts CONFIG REQUIRED)
if(ENABLE_TESTS)
    find_package(Catch2 CONFIG REQUIRED)
    if(ENABLE_SCRIPT_ENGINE)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
    endif()
endif()
add_subdirectory(shared)
add_subdirectory(praktor)
add_subdirectory(actions)
add_subdirectory(sdk)

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

install(FILES LICENSE NOTICE THIRD_PARTY_NOTICES.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/Praktor)
