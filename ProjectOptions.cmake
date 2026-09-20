include(CMakeDependentOption)

set(CMAKE_COLOR_DIAGNOSTICS ON)

# building the tests
option(ENABLE_TESTS "Enable the tests" OFF)
option(ENABLE_SCRIPT_ENGINE "Enable the TurboScript-backed workflow script engine" ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

find_package(Threads REQUIRED)
