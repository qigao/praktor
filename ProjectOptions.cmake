include(CMakeDependentOption)

set(CMAKE_COLOR_DIAGNOSTICS ON)

# building the tests
option(ENABLE_TESTS "Enable the tests" OFF)
option(PRAKTOR_ENABLE_SCRIPT "Enable TurboScript workflow script support" ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

find_package(Threads REQUIRED)
