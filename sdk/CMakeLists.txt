# Breaking C ABI v3: no praktor_lib, C++ ExecutionContext or fallback.
add_library(Praktor SHARED api/praktor_api.c)
target_include_directories(Praktor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/praktor/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(Praktor PUBLIC Salts::CMeta PRIVATE praktor_runtime)
target_compile_features(Praktor PUBLIC c_std_11)
set_target_properties(Praktor PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden OUTPUT_NAME Praktor)
target_compile_definitions(Praktor PRIVATE PRAKTOR_BUILD_SHARED=1)
install(TARGETS Praktor EXPORT PraktorTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES api/praktor.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${PROJECT_SOURCE_DIR}/praktor/include/praktor/runtime.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/praktor")
add_executable(praktor "${PROJECT_SOURCE_DIR}/praktor/main.c")
target_link_libraries(praktor PRIVATE Praktor)
target_compile_features(praktor PRIVATE c_std_11)
install(TARGETS praktor EXPORT PraktorTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(ENABLE_TESTS)
    add_executable(praktor_c_api_test test/praktor_c_api_test.c)
    target_link_libraries(praktor_c_api_test PRIVATE Praktor Salts::TinyTest)
    add_test(NAME praktor_c_api_test COMMAND praktor_c_api_test)
endif()
