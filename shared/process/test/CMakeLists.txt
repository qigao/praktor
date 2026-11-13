add_executable(praktor_service_control_test ${CMAKE_CURRENT_LIST_DIR}/service_control_test.c)
target_link_libraries(praktor_service_control_test PRIVATE praktor_service_control Salts::TinyTest)

add_test(NAME praktor_service_control_test COMMAND praktor_service_control_test)
set_tests_properties(praktor_service_control_test PROPERTIES TIMEOUT 30)
add_executable(praktor_service_fixture ${CMAKE_CURRENT_LIST_DIR}/service_fixture.c)
target_link_libraries(praktor_service_fixture PRIVATE Salts::Core)

add_executable(praktor_service_process_test ${CMAKE_CURRENT_LIST_DIR}/service_process_test.c)
target_link_libraries(praktor_service_process_test PRIVATE praktor_service_process_host Salts::TinyTest Salts::Core)
target_compile_definitions(praktor_service_process_test PRIVATE
    PRAKTOR_SERVICE_FIXTURE="$<TARGET_FILE:praktor_service_fixture>"
    PRAKTOR_SERVICE_STATE_PATH="${CMAKE_CURRENT_BINARY_DIR}/Service_fixture_state.txt")

add_dependencies(praktor_service_process_test praktor_service_fixture)
add_test(NAME praktor_service_process_test COMMAND praktor_service_process_test)
set_tests_properties(praktor_service_process_test PROPERTIES TIMEOUT 30)
add_executable(praktor_process_native_test process_native_test.c)
target_link_libraries(praktor_process_native_test PRIVATE praktor_process_native Salts::TinyTest Salts::Core)

add_test(NAME praktor_process_native_test COMMAND praktor_process_native_test)
set_tests_properties(praktor_process_native_test PROPERTIES TIMEOUT 30)
if(WIN32)
    # Test properties are scoped to the directory that declares the test.
    set_tests_properties(praktor_process_native_test PROPERTIES
        RESOURCE_LOCK praktor_managed_process_fixture)
endif()
add_executable(praktor_process_control_test process_control_test.c)
target_link_libraries(praktor_process_control_test PRIVATE praktor_process_native Salts::TinyTest Salts::Core)

add_test(NAME praktor_process_control_test COMMAND praktor_process_control_test)
set_tests_properties(praktor_process_control_test PROPERTIES TIMEOUT 30)
add_executable(praktor_process_worker_test process_worker_test.c)
target_link_libraries(praktor_process_worker_test PRIVATE praktor_process_driver Salts::TinyTest Salts::Core)

add_test(NAME praktor_process_worker_test COMMAND praktor_process_worker_test)
set_tests_properties(praktor_process_worker_test PROPERTIES TIMEOUT 30)
add_executable(praktor_process_driver_test process_driver_test.c)
target_link_libraries(praktor_process_driver_test PRIVATE praktor_process_driver Salts::TinyTest Salts::Core)

add_test(NAME praktor_process_driver_test COMMAND praktor_process_driver_test)
set_tests_properties(praktor_process_driver_test PROPERTIES TIMEOUT 30)
