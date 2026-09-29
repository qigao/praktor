cmake_minimum_required(VERSION 3.20)
project(praktor_package_smoke LANGUAGES C)
find_package(Praktor CONFIG REQUIRED)
add_executable(praktor_package_smoke main.c)
target_link_libraries(praktor_package_smoke PRIVATE Praktor::Praktor)
enable_testing()
add_test(NAME praktor_package_smoke COMMAND praktor_package_smoke
  "${CMAKE_CURRENT_SOURCE_DIR}/command.yml" "${CMAKE_CURRENT_SOURCE_DIR}/script.yml")
