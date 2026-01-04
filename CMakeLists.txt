cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0144 OLD)
cmake_policy(SET CMP0167 OLD) # for boost


set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)

# Set additional project information
set(CMAKE_PROJECT_VERSION_MAJOR 0)
set(CMAKE_PROJECT_VERSION_MINOR 3)
set(CMAKE_PROJECT_VERSION_PATCH 0)

set(PROJECT_VERSION
    ${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}.${CMAKE_PROJECT_VERSION_PATCH}
)

set(PROJECT_NAME "Prakter")
set(PROJECT_DESCRIPTION "A task & workflow orchestrator by DAG")
set(PROJECT_WEBSITE "http://github.com/qigao/project-cpp-template.git")
set(APPLICATION_LICENSE "Apache2 License")

# Set the project name and language
project(
    ${PROJECT_NAME}
    VERSION  ${PROJECT_VERSION}
    DESCRIPTION ${PROJECT_DESCRIPTION}
    HOMEPAGE_URL ${PROJECT_WEBSITE}
    LANGUAGES CXX C
)


# Output files directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_EXECUTABLE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")


# Add subdirectories
add_subdirectory(vendor)
# add_subdirectory(parser)
add_subdirectory(shared)
add_subdirectory(prakter)
add_subdirectory(modules)
