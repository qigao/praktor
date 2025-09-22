# Docker Plugin Example
project(weave_docker_plugin)

# Docker Plugin
add_library(weave_docker_plugin SHARED
    docker_plugin.cpp
)

target_include_directories(weave_docker_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/weave/include
)

target_link_libraries(weave_docker_plugin PRIVATE
    ${TARGET_NAME_LIB}
)

# Set plugin properties
set_target_properties(weave_docker_plugin PROPERTIES
    PREFIX ""  # Remove lib prefix on Unix
    SUFFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}"
    OUTPUT_NAME "docker_plugin"
)

# Install plugin to plugins directory
install(TARGETS weave_docker_plugin
    LIBRARY DESTINATION lib/weave/plugins
    RUNTIME DESTINATION lib/weave/plugins
)

# AWS Plugin Example (another plugin)
add_library(weave_aws_plugin SHARED
    aws_plugin.cpp
)

target_include_directories(weave_aws_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/weave/include
)

target_link_libraries(weave_aws_plugin PRIVATE
    ${TARGET_NAME_LIB}
)

set_target_properties(weave_aws_plugin PROPERTIES
    PREFIX ""
    SUFFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}"
    OUTPUT_NAME "aws_plugin"
)

install(TARGETS weave_aws_plugin
    LIBRARY DESTINATION lib/weave/plugins
    RUNTIME DESTINATION lib/weave/plugins
)

# Kubernetes Plugin Example
add_library(weave_k8s_plugin SHARED
    k8s_plugin.cpp
)

target_include_directories(weave_k8s_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/weave/include
)

target_link_libraries(weave_k8s_plugin PRIVATE
    ${TARGET_NAME_LIB}
)

set_target_properties(weave_k8s_plugin PROPERTIES
    PREFIX ""
    SUFFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}"
    OUTPUT_NAME "k8s_plugin"
)

install(TARGETS weave_k8s_plugin
    LIBRARY DESTINATION lib/weave/plugins
    RUNTIME DESTINATION lib/weave/plugins
)
