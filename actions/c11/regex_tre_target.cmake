if(NOT TARGET unofficial::tre::tre)
    # Root builds discover TRE through the vcpkg toolchain. Standalone C-only
    # proof projects run in a fresh CMake process, so also admit the exact
    # install tree populated by the root manifest in CI.
    set(_praktor_tre_hints)
    if(DEFINED ENV{VCPKG_INSTALLED_DIR} AND DEFINED ENV{TRIPLET})
        list(APPEND _praktor_tre_hints
            "$ENV{VCPKG_INSTALLED_DIR}/$ENV{TRIPLET}")
    endif()
    if(DEFINED ENV{TRIPLET})
        list(APPEND _praktor_tre_hints
            "${CMAKE_CURRENT_LIST_DIR}/../../vcpkg_installed/ci/$ENV{TRIPLET}"
            "${CMAKE_CURRENT_LIST_DIR}/../../vcpkg_installed/$ENV{TRIPLET}")
    endif()
    find_package(unofficial-tre CONFIG REQUIRED PATHS ${_praktor_tre_hints})
    unset(_praktor_tre_hints)
endif()

if(NOT TARGET praktor_regex_tre)
    add_library(praktor_regex_tre STATIC
        ${CMAKE_CURRENT_LIST_DIR}/regex_tre.c
        ${CMAKE_CURRENT_LIST_DIR}/regex_tre.h)
    target_include_directories(praktor_regex_tre PUBLIC ${CMAKE_CURRENT_LIST_DIR})
    target_link_libraries(praktor_regex_tre PRIVATE unofficial::tre::tre)
    target_compile_features(praktor_regex_tre PUBLIC c_std_11)
    set_target_properties(praktor_regex_tre PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON)
endif()
