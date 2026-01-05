#ifndef __TEST_HELPER_H__
#define __TEST_HELPER_H__
#include "yml/task_yaml.hpp"

#include <algorithm>
#include <string>
#include <vector>

inline bool containsAny(std::string const& target, std::vector<std::string> const& stringVec) {
    return std::ranges::any_of(stringVec, [&target](std::string const& str) {
        return target.find(str) != std::string::npos;   // Check if str is found in target
    });
}

constexpr char const* yml_demo_data = R"(
name: "Cake example"
description: "A project for learning Praktor"

variables:
    MY_VAR: "Hello, World!"
    MY_VAR2: "Another value"
    MY_VAR3: "Yet another value"

dotEnv:
    - .env
    - .env.cmake

tasks:
  - name: hello
    description: "Print Hello, World!"
    command: "echo Hello, World!"

  - name: config
    description: "Configure the project"
    command: "cmake -B build -S ."

  - name: build
    description: "Build the project"
    depends_on: [config]
    command: "cmake --build build --config Release"

  - name: test
    description: "Run the tests"
    depends_on: [build]
    command: "ctest --output-on-failure"

  - name: clean
    description: "Clean the project"
    depends_on: [test]
    command: "rm -rf build"
)";

#endif   // __TEST_HELPER_H__
