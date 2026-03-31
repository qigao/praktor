cmake_minimum_required(VERSION 3.16)

# Find TinyTest
find_package(TurboNet CONFIG REQUIRED)

add_executable(test_ast test_ast.cpp)
target_link_libraries(test_ast PRIVATE btdsl TurboNet::TinyTest)
target_compile_features(test_ast PRIVATE cxx_std_17)
add_test(NAME test_ast COMMAND test_ast)


# Set test properties
set_target_properties(
    test_ast
    PROPERTIES FOLDER "btdsl/tests"
)
