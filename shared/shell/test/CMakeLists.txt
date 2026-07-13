cmake_add_test(
    SOURCES shell_executor_test.cpp
    LIBS Catch2::Catch2WithMain praktor_shell
    FEATURES cxx_std_17
    FOLDER "shared/tests"
)
