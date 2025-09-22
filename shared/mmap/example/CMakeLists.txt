file(GLOB EXAMPLE_SOURCES *_example.cpp)

foreach(example_source ${EXAMPLE_SOURCES})
    get_filename_component(source_name ${example_source} NAME_WE)
    message("-- Adding example: ${source_name} to ${TARGET_NAME}")
    set(EXAMPLE_NAME ${source_name})
    add_executable(${EXAMPLE_NAME} ${example_source})
    target_link_libraries(
        ${EXAMPLE_NAME} PRIVATE ${TARGET_NAME} pubcxx

    )
    target_include_directories(
        ${EXAMPLE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
    set_target_properties(
        ${EXAMPLE_NAME} PROPERTIES FOLDER
                                   "shared/${TARGET_NAME}/example"
    )

endforeach()
