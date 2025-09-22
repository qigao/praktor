#include "yml/task_parser.hpp"
#include "yml/task_yaml.hpp"

#include <catch2/catch_all.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

void verify_task_assertions(const Workflow& workflow) {
  // Verify inputs
  REQUIRE(workflow.inputs.size() == 1);
  CHECK(workflow.inputs[0].name == "environment");
  CHECK(workflow.inputs[0].type == "string");
  CHECK(workflow.inputs[0].default_value == "dev");

  // Verify variables
  REQUIRE(workflow.variables.size() == 1);
  CHECK(workflow.variables.at("DOCKER_REGISTRY") == "my.registry.com");

  // Verify defaults
  CHECK(workflow.defaults.retries.count == 1);

  // Verify tasks
  REQUIRE(workflow.tasks.size() == 3);

  // Task 1: build
  const auto& build_task = workflow.tasks[0];
  CHECK(build_task.name == "build");
  CHECK(build_task.type == "run_command");
  REQUIRE(std::holds_alternative<RunCommandParams>(build_task.specifics));
  const auto& build_params = std::get<RunCommandParams>(build_task.specifics);
  REQUIRE(std::holds_alternative<std::string>(build_params.command));
  CHECK(std::get<std::string>(build_params.command) == "make build");

  // Task 2: test
  const auto& test_task = workflow.tasks[1];
  CHECK(test_task.name == "test");
  REQUIRE(test_task.depends_on.size() == 1);
  CHECK(test_task.depends_on[0] == "build");
  CHECK(test_task.when == "{{ environment }} == 'dev'");
  REQUIRE(std::holds_alternative<RunCommandParams>(test_task.specifics));
  const auto& test_params = std::get<RunCommandParams>(test_task.specifics);
  REQUIRE(std::holds_alternative<StrList>(test_params.command));
  CHECK(std::get<StrList>(test_params.command).size() == 2);
  CHECK(std::get<StrList>(test_params.command)[0] == "make");

  // Task 3: create_log_dir
  const auto& create_task = workflow.tasks[2];
  CHECK(create_task.name == "create_log_dir");
  CHECK(create_task.type == "create_directory");
  REQUIRE(std::holds_alternative<CreateDirectoryParams>(
      create_task.specifics));
  const auto& create_params = std::get<CreateDirectoryParams>(create_task.specifics);
  CHECK(create_params.path == "/var/log/my_app");
  CHECK(create_params.parents == true);
}

TEST_CASE("TaskParser New Grammar Test", "[parser]")
{
  // Start with a minimal YAML to test parsing
  std::string test_yaml_content = 
    "tasks:\n"
    "  - name: build\n"
    "    type: run_command\n"
    "    command: \"make build\"\n";

  std::string test_file = "parser_test.yml";
  std::ofstream out(test_file);
  out << test_yaml_content;
  out.close();

  SECTION("Parse a minimal workflow file")
  {
    // Debug: print the YAML content
    std::cout << "Generated YAML content:\n" << test_yaml_content << "\n";
    std::cout << "=== End of YAML ===\n";
    
    REQUIRE_NOTHROW([&]() {
      Workflow workflow = TaskParser::parseFile(test_file);
      
      // Basic checks
      REQUIRE(workflow.tasks.size() == 1);
      CHECK(workflow.tasks[0].name == "build");
      CHECK(workflow.tasks[0].type == "run_command");
    }());
  }

  remove(test_file.c_str());
}
