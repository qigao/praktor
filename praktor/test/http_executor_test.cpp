#include "workflow_runner.hpp"
#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>

namespace {
std::filesystem::path createTempDir() {
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::filesystem::path dir = std::filesystem::temp_directory_path() / ("praktor_http_test_" + std::to_string(now));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path);
    out << content;
}
}

TEST_CASE("HTTP Chained Request", "[executor][http]") {
    auto dir = createTempDir();
    auto workflow_path = dir / "chain.yml";

    writeFile(workflow_path, R"(
tasks:
  - name: auth_mock
    http:
      url: "https://httpbin.org/get?token=secret_abc_123"
      method: GET

  - name: debug_output
    depends_on: ["auth_mock"]
    script:
      source: |
        print("DEBUG: auth_mock body:", context.get("tasks.auth_mock.outputs.body"));
        print("DEBUG: auth_mock token:", context.get("tasks.auth_mock.outputs.data.args.token"));

  - name: create_resource
    depends_on: ["debug_output"]
    http:
      url: "https://httpbin.org/post"
      method: POST
      headers:
        Authorization: "Bearer {{ tasks.auth_mock.outputs.data.args.token }}"
      body: '{"name": "test_resource"}'

  - name: verify_chain
    depends_on: ["create_resource"]
    script:
      source: |
        const token = context.get("tasks.create_resource.outputs.data.headers.Authorization");
        const name = context.get("tasks.create_resource.outputs.data.json.name");

        print("Auth Token used:", token);
        print("Resource name:", name);

        if (token !== "Bearer secret_abc_123") {
            fail("Auth token not propagated correctly: " + token);
        }
        if (name !== "test_resource") {
            fail("Post body not propagated correctly: " + name);
        }
)");

    WorkflowRunner runner(workflow_path.string());

    // Note: This test requires internet access to httpbin.org
    bool success = runner.run();

    if (!success) {
        TLOG_ERROR("HTTP Chained Request test failed. Check cross-task data propagation.");
    }

    REQUIRE(success);

    std::filesystem::remove_all(dir);
}

TEST_CASE("HTTP Error Handling", "[executor][http]") {
    auto dir = createTempDir();
    auto workflow_path = dir / "error.yml";

    writeFile(workflow_path, R"(
tasks:
  - name: fail_404
    http:
      url: "https://httpbin.org/status/404"
      method: GET
)");

    WorkflowRunner runner(workflow_path.string());
    // Should return false because 404 is considered task failure by default in my implementation (status >= 300)
    // Wait, in my HttpExecutor::execute:
    REQUIRE_FALSE(runner.run());

    std::filesystem::remove_all(dir);
}

TEST_CASE("HTTP Script Hooks", "[executor][http]") {
    auto dir = createTempDir();
    auto workflow_path = dir / "hooks.yml";

    writeFile(workflow_path, R"(
tasks:
  - name: script_modification
    http:
      url: "https://httpbin.org/get"
      method: GET
      script: |
        console.log("Modifying request...");
        request.url = request.url + "?foo=bar";
        request.addHeader("X-Script-Added", "True");
      test: |
        console.log("Testing response...", response.status);
        if (response.status !== 200) {
            fail("Expected 200 OK");
        }
        var body = JSON.parse(response.body);
        if (body.args.foo !== "bar") {
            fail("Query param modification failed");
        }
        if (body.headers["X-Script-Added"] !== "True") {
            fail("Header modification failed");
        }
)");

    WorkflowRunner runner(workflow_path.string());
    // This requires internet access
    bool success = runner.run();
    REQUIRE(success);

    std::filesystem::remove_all(dir);
}
