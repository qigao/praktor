set(TARGET_NAME_PISTOL pistol)

# Need to compile pistol as SHARED library
add_library(${TARGET_NAME_PISTOL} SHARED
    api/praktor_api.cpp
)

# Include directories
# We set api/ to PUBLIC so that external users linking to pistol can just #include "praktor_api.h"
target_include_directories(
    ${TARGET_NAME_PISTOL}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
           $<INSTALL_INTERFACE:include>
)

# Link to praktor_lib to get WorkflowRunner, etc.
target_link_libraries(${TARGET_NAME_PISTOL} PRIVATE praktor_lib
PUBLIC TurboScript::ExprTk)

# Export definition for PRAKTOR_API
target_compile_definitions(${TARGET_NAME_PISTOL} PRIVATE PRAKTOR_EXPORTS)

set_target_properties(${TARGET_NAME_PISTOL} PROPERTIES FOLDER "pistol")

install(
    TARGETS ${TARGET_NAME_PISTOL}
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES
    DESTINATION include
)

install(FILES api/praktor_api.h DESTINATION include)

install(DIRECTORY templates examples DESTINATION share/pistol)

install(FILES README.md hello-world.yml DESTINATION share/pistol)

if(ENABLE_TESTS)
    add_subdirectory(test)
endif()
