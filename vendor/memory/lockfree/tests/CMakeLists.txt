find_package(Catch2 REQUIRED)

if(NOT DEFINED TEST_MT_TRANSFER_CNT)
    set(TEST_MT_TRANSFER_CNT 10240)
    add_definitions(-DTEST_MT_TRANSFER_CNT=${TEST_MT_TRANSFER_CNT})

endif()

file(GLOB_RECURSE TEST_SOURCES *.cpp)

foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)

    set(executable_name "${test_name}.${TARGET_NAME}")
    add_executable(${executable_name} ${test_source})
    target_link_libraries(${executable_name} PRIVATE Catch2::Catch2WithMain ${TARGET_NAME})
    add_test(NAME ${executable_name} COMMAND ${executable_name})

    set_target_properties(
        ${executable_name} PROPERTIES FOLDER
                                               "vendor/memory/${TARGET_NAME}"
    )
endforeach()
