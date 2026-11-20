cmake_minimum_required(VERSION 3.20)
include(CMakeOptions.cmake)

project(
    Praktor
    VERSION 0.2.0
    DESCRIPTION "workflow engine based on C11"
    HOMEPAGE_URL "https://github.com/qigao/praktor"
    LANGUAGES C
)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CmakeUtils)
find_package(Threads REQUIRED)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(BUILD_TESTS)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    enable_testing()
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# A valid cached package directory can bypass the selected SDK's search paths.
function(praktor_require_package_root package selection)
    file(REAL_PATH "${${package}_CONFIG}" config_file)
    if(CMAKE_HOST_WIN32)
        string(TOLOWER "${config_file}" config_file)
    endif()
    foreach(root IN LISTS ARGN)
        if(root STREQUAL "")
            continue()
        endif()
        file(REAL_PATH "${root}" sdk_root)
        if(CMAKE_HOST_WIN32)
            string(TOLOWER "${sdk_root}" sdk_root)
        endif()
        cmake_path(IS_PREFIX sdk_root "${config_file}" NORMALIZE in_sdk)
        if(in_sdk)
            return()
        endif()
    endforeach()
    message(
        FATAL_ERROR
        "${package} resolved to '${${package}_CONFIG}' outside ${selection} ('${ARGN}'). "
        "Check the SDK selection and run 'cmake --fresh --preset <configure-preset>'."
    )
endfunction()

find_package(
    SaltsBase "$ENV{SALTS_BASE_VERSION}" EXACT CONFIG REQUIRED PATHS "$ENV{SALTS_BASE_ROOT}" NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
)
praktor_require_package_root(SaltsBase SALTS_BASE_ROOT "$ENV{SALTS_BASE_ROOT}")
find_package(
    CMeta "$ENV{CMETA_VERSION}" EXACT CONFIG REQUIRED PATHS "$ENV{CMETA_ROOT}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
)
praktor_require_package_root(CMeta CMETA_ROOT "$ENV{CMETA_ROOT}")
find_package(
    SaltsUtils "$ENV{SALTS_UTILS_VERSION}" EXACT CONFIG REQUIRED PATHS "$ENV{SALTS_UTILS_ROOT}" NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
)
praktor_require_package_root(SaltsUtils SALTS_UTILS_ROOT "$ENV{SALTS_UTILS_ROOT}")

add_subdirectory(shared)
add_subdirectory(praktor)
add_subdirectory(app)

set(PRAKTOR_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/Praktor")
install(
    EXPORT PraktorTargets
    FILE PraktorTargets.cmake
    NAMESPACE Praktor::
    DESTINATION "${PRAKTOR_INSTALL_CMAKEDIR}"
)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PraktorConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/PraktorConfig.cmake"
    INSTALL_DESTINATION "${PRAKTOR_INSTALL_CMAKEDIR}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PraktorConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/PraktorConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/PraktorConfigVersion.cmake"
        DESTINATION "${PRAKTOR_INSTALL_CMAKEDIR}"
)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION "${CMAKE_INSTALL_DATADIR}/Praktor")
