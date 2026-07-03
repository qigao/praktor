find_package(Catch2 CONFIG REQUIRED)

add_executable(shell_executor_test shell_executor_test.cpp)
target_link_libraries(shell_executor_test PRIVATE Catch2::Catch2WithMain praktor_shell)
target_compile_features(shell_executor_test PRIVATE cxx_std_17)

add_test(NAME shell_executor_test COMMAND shell_executor_test)

set_target_properties(shell_executor_test PROPERTIES FOLDER "shared/tests")
