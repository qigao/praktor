cmake_minimum_required(VERSION 3.20)
set(CMAKE_PROJECT_VERSION_MAJOR 0)
set(CMAKE_PROJECT_VERSION_MINOR 5)
set(CMAKE_PROJECT_VERSION_PATCH 0)
set(PROJECT_VERSION ${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}.${CMAKE_PROJECT_VERSION_PATCH})
project(Praktor VERSION ${PROJECT_VERSION}
    DESCRIPTION "A native C11 CFlow/CMeta workflow runtime"
    HOMEPAGE_URL "https://github.com/qigao/praktor"
    LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(ProjectOptions.cmake)
include(CmakeUtils)
add_subdirectory(tools)
include(FindTools)
if(ENABLE_SCRIPT_ENGINE)
    message(FATAL_ERROR "C11 Praktor has no C++ TurboScript fallback")
endif()
if(ENABLE_TESTS)
    enable_testing()
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(_root IN ITEMS SALTS_ROOT SALTS_UTILS_ROOT)
    if(NOT DEFINED ENV{${_root}} OR NOT IS_DIRECTORY "$ENV{${_root}}")
        message(FATAL_ERROR "${_root} must identify an installed native C11 SDK")
    endif()
    file(TO_CMAKE_PATH "$ENV{${_root}}" _praktor_${_root})
endforeach()
find_package(Salts CONFIG REQUIRED
    PATHS "${_praktor_SALTS_ROOT}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
find_package(SaltsUtils CONFIG REQUIRED
    PATHS "${_praktor_SALTS_UTILS_ROOT}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
foreach(required_target IN ITEMS Salts::CMeta Salts::CFlow
        Salts::Configurator Salts::BehaviorTree Salts::Process
        Salts::JsonParser Salts::CYaml)
    if(NOT TARGET ${required_target})
        message(FATAL_ERROR "Missing installed native dependency: ${required_target}")
    endif()
endforeach()
include(PraktorYamlTarget)
add_subdirectory(shared/process)
add_subdirectory(praktor)
add_subdirectory(actions)
add_subdirectory(praktor/src/runtime)
add_subdirectory(sdk)
add_subdirectory(tools/schema)
if(ENABLE_TESTS)
    add_subdirectory(praktor/test)
endif()
set(PRAKTOR_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/Praktor")
install(EXPORT PraktorTargets FILE PraktorTargets.cmake NAMESPACE Praktor::
    DESTINATION "${PRAKTOR_INSTALL_CMAKEDIR}")
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PraktorConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PraktorConfig.cmake"
    INSTALL_DESTINATION "${PRAKTOR_INSTALL_CMAKEDIR}")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PraktorConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/PraktorConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PraktorConfigVersion.cmake"
    DESTINATION "${PRAKTOR_INSTALL_CMAKEDIR}")
install(FILES LICENSE NOTICE THIRD_PARTY_NOTICES.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/Praktor")
