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
target_link_libraries(${TARGET_NAME_PISTOL} PRIVATE praktor_lib TurboScript::ExprTk)

# Export definition for PRAKTOR_API
target_compile_definitions(${TARGET_NAME_PISTOL} PRIVATE PRAKTOR_EXPORTS)

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
    FILES api/praktor.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(ENABLE_TESTS)
    add_subdirectory(test)
endif()
