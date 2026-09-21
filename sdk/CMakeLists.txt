set(TARGET_NAME_SDK Praktor)

# Build the stable Praktor C SDK as a shared library
add_library(${TARGET_NAME_SDK} SHARED
    api/praktor_api.cpp
)

# Include directories
# The public API is the installed praktor.h header.
target_include_directories(
    ${TARGET_NAME_SDK}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
           $<INSTALL_INTERFACE:include>
)

# Link to praktor_lib to get WorkflowRunner, etc.
target_link_libraries(
    ${TARGET_NAME_SDK}
    PRIVATE praktor_lib
    INTERFACE
        $<LINK_ONLY:Threads::Threads>
        $<LINK_ONLY:Salts::Core>
        $<LINK_ONLY:Salts::JsonParser>
        $<LINK_ONLY:Salts::CYaml>
        $<LINK_ONLY:Salts::CYamlJsonAdapter>
        $<LINK_ONLY:Salts::XmlParser>
        $<LINK_ONLY:Salts::CsvParser>
        $<LINK_ONLY:Salts::Mustache>
        $<LINK_ONLY:CHttp::Client>
        $<LINK_ONLY:Salts::CNet>
        $<LINK_ONLY:CHttp::S3>
)
if(ENABLE_SCRIPT_ENGINE)
    target_link_libraries(
        ${TARGET_NAME_SDK}
        INTERFACE $<LINK_ONLY:TurboScript::TurboScript>
    )
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(
        ${TARGET_NAME_SDK}
        INTERFACE $<LINK_ONLY:${CMAKE_DL_LIBS}>
    )
endif()

# Only the producer needs an explicit marker. Consumers import by default.
target_compile_definitions(${TARGET_NAME_SDK}
    PRIVATE PRAKTOR_BUILD_SHARED
)

if(MSVC)
    # praktor.exe and Praktor.dll otherwise contend for the same case-insensitive
    # bin/praktor.{ilk,pdb} paths when Ninja links Debug targets in parallel.
    set_target_properties(${TARGET_NAME_SDK} PROPERTIES
        PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    target_link_options(${TARGET_NAME_SDK} PRIVATE
        "/ILK:${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME_SDK}.ilk"
    )
endif()

cmake_config_target(${TARGET_NAME_SDK} NO_INSTALL NO_VERSION FOLDER "sdk")

install(
    TARGETS ${TARGET_NAME_SDK}
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
