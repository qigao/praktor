set(praktor_pcre2_minimum 10.46)
find_package(PCRE2 "${praktor_pcre2_minimum}" CONFIG REQUIRED COMPONENTS 8BIT)
if(VCPKG_TARGET_TRIPLET AND DEFINED VCPKG_INSTALLED_DIR)
    set(praktor_pcre2_roots "")
    if(NOT VCPKG_INSTALLED_DIR STREQUAL "")
        list(APPEND praktor_pcre2_roots "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    endif()
    if(NOT "$ENV{PRAKTOR_VCPKG_INSTALLED_DIR}" STREQUAL "")
        list(APPEND praktor_pcre2_roots "$ENV{PRAKTOR_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    endif()
    praktor_require_package_root(PCRE2 "the selected vcpkg roots for ${VCPKG_TARGET_TRIPLET}" ${praktor_pcre2_roots})
endif()

# Record package discovery from the normal configure for the Statechart audit.
set(praktor_cache_version "$ENV{VCPKG_CACHE_VERSION}")
string(REPLACE "." ";" praktor_pcre2_minimum_parts "${praktor_pcre2_minimum}")
list(GET praktor_pcre2_minimum_parts 0 praktor_pcre2_minimum_major)
list(GET praktor_pcre2_minimum_parts 1 praktor_pcre2_minimum_minor)
string(
    CONFIGURE
        [=[
{
  "salts_base": {"version": "@SaltsBase_VERSION@", "exact": "\\A(\\Q@SaltsBase_VERSION@\\E)\\z"},
  "cmeta": {"version": "@CMeta_VERSION@", "exact": "\\A(\\Q@CMeta_VERSION@\\E)\\z"},
  "salts_utils": {"version": "@SaltsUtils_VERSION@", "exact": "\\A(\\Q@SaltsUtils_VERSION@\\E)\\z"},
  "vcpkg_cache_selection": {"version": "@praktor_cache_version@", "exact": "\\A(\\Q@praktor_cache_version@\\E)\\z"},
  "pcre2": {
    "version": "@PCRE2_VERSION@", "major": @PCRE2_VERSION_MAJOR@, "minor": @PCRE2_VERSION_MINOR@,
    "minimum_major": @praktor_pcre2_minimum_major@, "minimum_minor": @praktor_pcre2_minimum_minor@
  },
  "compiler": {
    "id": "@CMAKE_C_COMPILER_ID@", "version": "@CMAKE_C_COMPILER_VERSION@",
    "platform": "@CMAKE_C_PLATFORM_ID@"
  },
  "triplet": "@VCPKG_TARGET_TRIPLET@"
}
]=]
        praktor_configured_versions
    @ONLY
)
file(
    GENERATE
    OUTPUT "${PROJECT_BINARY_DIR}/praktor-upstream-versions.json"
    CONTENT "${praktor_configured_versions}"
)

file(GLOB_RECURSE PRAKTOR_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB_RECURSE PRAKTOR_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${PRAKTOR_SOURCES} ${PRAKTOR_HEADERS})

add_library(Praktor SHARED ${PRAKTOR_SOURCES} ${PRAKTOR_HEADERS})
target_include_directories(
    Praktor
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
    PRIVATE src/runtime src/profile src/util
)
target_link_libraries(
    Praktor
    PUBLIC CMeta::CMeta
    PRIVATE CMeta::ACE CMeta::CFlow CMeta::CSTL Salts::Core Salts::JsonParser Salts::CYaml Salts::DotEnvParser
            Salts::CYamlJsonAdapter Salts::XmlParser Salts::CsvParser Salts::NativeIO Salts::Selector Salts::QueryVM
            Salts::BehaviorTree praktor_process_driver praktor_process_retainer praktor_process_native PCRE2::8BIT
)
if(UNIX)
    target_link_libraries(Praktor PRIVATE m)
endif()
target_compile_features(Praktor PUBLIC c_std_11)
target_compile_definitions(Praktor PRIVATE PRAKTOR_BUILD_SHARED=1)
set_target_properties(Praktor PROPERTIES C_VISIBILITY_PRESET hidden FOLDER "praktor")
install(
    TARGETS Praktor
    EXPORT PraktorTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(FILES include/praktor.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(BUILD_TESTS)
    add_subdirectory(test)
endif()
