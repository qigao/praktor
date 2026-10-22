#include "praktor_test.hpp"
#include "executors/download_executor.hpp"
#include "executors/download_executor_internal.hpp"


#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

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

[[maybe_unused]] static void test_download_executor_rejects_non_https_urls_before_i_o_1() {
    DownloadParams params;
    params.url = "http://example.test/package.zip";
    params.path = "package.zip";

    WorkflowContext context;
    Praktor::Execution::DownloadExecutor executor;
    const auto result = executor.execute(makeDownloadTask(std::move(params)), context);

    check((!(result.success)));
    check((result.error_message.find("absolute HTTPS URL") != std::string::npos));
}

spec("download executor rejects non-HTTPS URLs before I/O") {
    it("complete case") { test_download_executor_rejects_non_https_urls_before_i_o_1(); }
}

[[maybe_unused]] static void test_download_executor_honors_overwrite_guard_before_network_i_o_2() {
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

    check((!(result.success)));
    check((result.error_message.find("overwrite is false") != std::string::npos));
}

spec("download executor honors overwrite guard before network I/O") {
    it("complete case") { test_download_executor_honors_overwrite_guard_before_network_i_o_2(); }
}

[[maybe_unused]] static void test_download_checksum_accepts_complete_files_and_sha_256_block_boundaries_3() {
    struct Vector {
        std::string bytes;
        const char* digest;
    };
    const Vector vectors[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {std::string(65536, 'a'), "bf718b6f653bebc184e1479f1935b8da974d701b893afcf49e701f3e2f9f9c5a"},
        {std::string(65537, 'a'), "008ffc88d3c96a9f307524eb361e47c5222a887fc45fa0c1fb8d429c5c23b430"},
    };
    struct TemporaryFile {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("praktor-checksum-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        ~TemporaryFile() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } file;

    for (const auto& vector : vectors) {
        info("%s", praktor_test::message([&](std::ostream& out) { out << "file bytes: " << vector.bytes.size(); }).c_str());
        {
            std::ofstream output(file.path, std::ios::binary | std::ios::trunc);
            check((output.is_open()));
            output.write(vector.bytes.data(), static_cast<std::streamsize>(vector.bytes.size()));
            check((output.good()));
        }
        std::string error;
        const auto digest = Praktor::Execution::Internal::sha256File(file.path, &error);
        check((error.empty()));
        check((digest == vector.digest));
    }
}

spec("download checksum accepts complete files and SHA-256 block boundaries") {
    it("complete case") { test_download_checksum_accepts_complete_files_and_sha_256_block_boundaries_3(); }
}

[[maybe_unused]] static void test_download_checksum_reports_a_missing_file_instead_of_an_empty_file_digest_4() {
    const auto path = std::filesystem::temp_directory_path() /
        ("praktor-missing-checksum-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string error;
    check((Praktor::Execution::Internal::sha256File(path, &error).empty()));
    check((error.find("failed to open") != std::string::npos));
}

spec("download checksum reports a missing file instead of an empty-file digest") {
    it("complete case") { test_download_checksum_reports_a_missing_file_instead_of_an_empty_file_digest_4(); }
}
