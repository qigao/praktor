find_package(unofficial-tre CONFIG REQUIRED)

file(GLOB_RECURSE PRAKTOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB_RECURSE PRAKTOR_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${PRAKTOR_SOURCES} ${PRAKTOR_HEADERS})

add_library(Praktor SHARED ${PRAKTOR_SOURCES} ${PRAKTOR_HEADERS})
target_include_directories(Praktor
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        src/runtime src/profile src/util)
target_link_libraries(Praktor
    PUBLIC CMeta::CMeta
    PRIVATE CMeta::ACE CMeta::CFlow CMeta::CSTL
        Salts::Core Salts::JsonParser Salts::CYaml Salts::DotEnvParser
        Salts::CYamlJsonAdapter Salts::XmlParser Salts::CsvParser
        Salts::NativeIO Salts::BehaviorTree
        praktor_process_driver praktor_process_retainer praktor_process_native
        unofficial::tre::tre)
if(UNIX)
    target_link_libraries(Praktor PRIVATE m)
endif()
target_compile_features(Praktor PUBLIC c_std_11)
target_compile_definitions(Praktor PRIVATE PRAKTOR_BUILD_SHARED=1)
set_target_properties(Praktor PROPERTIES
    C_VISIBILITY_PRESET hidden OUTPUT_NAME Praktor FOLDER "praktor")
install(TARGETS Praktor EXPORT PraktorTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES include/praktor.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(BUILD_TESTS)
    add_subdirectory(test)
endif()
