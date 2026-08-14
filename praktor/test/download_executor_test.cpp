#include "executors/download_executor.hpp"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

Task makeDownloadTask(DownloadParams params) {
    Task task;
    task.name = "download";
    task.action = TaskAction::Download;
    task.declared_runner = "download";
    task.specifics = std::move(params);
    task.source_path = (std::filesystem::temp_directory_path() / "workflow.yml").string();
    return task;
}

}  // namespace

TEST_CASE("download executor rejects non-HTTPS URLs before I/O") {
    DownloadParams params;
    params.url = "http://example.test/package.zip";
    params.path = "package.zip";

    WorkflowContext context;
    Praktor::Execution::DownloadExecutor executor;
    const auto result = executor.execute(makeDownloadTask(std::move(params)), context);

    CHECK_FALSE(result.success);
    CHECK(result.error_message.find("absolute HTTPS URL") != std::string::npos);
}

TEST_CASE("download executor honors overwrite guard before network I/O") {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto destination = std::filesystem::temp_directory_path() /
                             ("praktor-download-existing-" + unique + ".zip");
    {
        std::ofstream output(destination, std::ios::binary);
        output << "existing";
    }

    DownloadParams params;
    params.url = "https://example.test/package.zip";
    params.path = destination.string();
    params.overwrite = false;

    WorkflowContext context;
    Praktor::Execution::DownloadExecutor executor;
    const auto result = executor.execute(makeDownloadTask(std::move(params)), context);
    std::error_code ec;
    std::filesystem::remove(destination, ec);

    CHECK_FALSE(result.success);
    CHECK(result.error_message.find("overwrite is false") != std::string::npos);
}
