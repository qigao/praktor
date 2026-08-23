set(TARGET_NAME_PISTOL Praktor)

# Need to compile pistol as SHARED library
add_library(${TARGET_NAME_PISTOL} SHARED
    api/praktor_api.cpp
)

# Include directories
# The public API is the installed praktor.h header.
target_include_directories(
    ${TARGET_NAME_PISTOL}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
           $<INSTALL_INTERFACE:include>
)

# Link to praktor_lib to get WorkflowRunner, etc.
target_link_libraries(
    ${TARGET_NAME_PISTOL}
    PRIVATE praktor_lib
)

# Keep the producer/consumer side of Praktor's shared ABI explicit.
target_compile_definitions(${TARGET_NAME_PISTOL}
    PRIVATE PRAKTOR_BUILD_SHARED
    INTERFACE PRAKTOR_USE_SHARED
)

if(MSVC)
    # praktor.exe and Praktor.dll otherwise contend for the same case-insensitive
    # bin/praktor.{ilk,pdb} paths when Ninja links Debug targets in parallel.
    set_target_properties(${TARGET_NAME_PISTOL} PROPERTIES
        PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    target_link_options(${TARGET_NAME_PISTOL} PRIVATE
        "/ILK:${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME_PISTOL}.ilk"
    )
endif()

cmake_config_target(${TARGET_NAME_PISTOL} NO_INSTALL NO_VERSION FOLDER "pistol")

install(
    TARGETS ${TARGET_NAME_PISTOL}
    EXPORT PraktorTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    FILES api/praktor.h api/praktor_export.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(ENABLE_TESTS)
    add_subdirectory(test)
endif()
