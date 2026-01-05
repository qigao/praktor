add_library(fmtlog STATIC)
add_library(fmtlog::fmtlog ALIAS fmtlog)

target_include_directories(
    fmtlog PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_sources(
    fmtlog
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/fmtlog.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/fmtlog-inl.h
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/fmtlog.cpp
)

target_compile_features(fmtlog PUBLIC cxx_std_11)

# Find dependencies
find_package(fmt CONFIG REQUIRED)
# Link dependencies
target_link_libraries(
    fmtlog
     PUBLIC
    fmt::fmt-header-only
)
set_target_properties(fmtlog PROPERTIES LINKER_LANGUAGE CXX)

set_target_properties(fmtlog PROPERTIES FOLDER "shared/fmtlog")
