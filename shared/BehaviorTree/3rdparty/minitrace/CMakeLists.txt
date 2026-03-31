add_library(minitrace STATIC
    minitrace.cpp
)

add_library(minitrace::minitrace ALIAS minitrace)

target_include_directories(minitrace
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(minitrace
    PRIVATE
        MTR_ENABLED=True
)

set_property(TARGET minitrace
    PROPERTY
        POSITION_INDEPENDENT_CODE ON
)
set_target_properties(minitrace PROPERTIES
    PREFIX ""  # No 'lib' prefix on Unix
    FOLDER "shared/BT"
)
