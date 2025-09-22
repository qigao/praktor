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
version: "1.0"
name: "Cake example"
description: "A project for learning Cake"
includes:
   http: ./examples/http/weave.yml
   clang: ./config/tasks/Taskfile.lint.clang.yml
   cmake: ./config/tasks/Taskfile.lint.cmake.yml
   coverage: ./config/tasks/Taskfile.coverage.yml
   setup: ./config/tasks/Taskfile.setup.yml
   shared: ./shared/Taskfile.yml

dotEnv:
    - .env
    - .env.cmake
vars:
    MY_VAR: "Hello, World!"
    MY_VAR2: "Another value"
    MY_VAR3: "Yet another value"

tasks:
    hello:
      desc: Print "Hello, World!"
      cmds: echo "Hello, World!"
    config:
      desc: Configure the project
      uses:  configure.lua
    build:
      desc: Build the project
      depends: config
      cmds: cmake --build . --config Release
    data:
      desc: Run the tests
      depends: build
      uses: data.lua
    test:
      desc: Run the tests
      depends: build
      cmds: ctest --output-on-failure
    clean:
      desc: Clean the project
      depends: test
      cmds: rm -rf build
)";

#endif   // __TEST_HELPER_H__
