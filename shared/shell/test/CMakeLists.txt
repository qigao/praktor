cmake_add_test(
    SOURCES shell_executor_test.cpp
    LIBS praktor_test_support praktor_shell
    FEATURES cxx_std_17
    FOLDER "shared/tests"
)
