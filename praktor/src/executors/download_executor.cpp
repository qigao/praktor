#include "executors/download_executor.hpp"

#include "util/path_utils.hpp"
#include "util/variable_substitution.hpp"

#include <turbo_crypto.h>
#include <turbo_http.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Praktor::Execution {
namespace {

struct DownloadSink {
  std::ofstream output;
  std::uint64_t bytes_written = 0;
  bool failed = false;
};

void writeDownloadChunk(const char* data, std::size_t size, void* user_data) {
  auto* sink = static_cast<DownloadSink*>(user_data);
  if (sink == nullptr || sink->failed || data == nullptr || size == 0) {
    return;
  }
  sink->output.write(data, static_cast<std::streamsize>(size));
  sink->failed = !sink->output.good();
  if (!sink->failed) {
    sink->bytes_written += static_cast<std::uint64_t>(size);
  }
}

bool isAbsoluteHttpsUrl(std::string_view url) {
  constexpr std::string_view prefix = "https://";
  if (!url.starts_with(prefix) || url.size() == prefix.size()) {
    return false;
  }
  return std::none_of(url.begin(), url.end(), [](unsigned char ch) {
    return std::iscntrl(ch) != 0 || std::isspace(ch) != 0;
  });
}

std::string lowerTrim(std::string value) {
  const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  }).base();
  if (first >= last) {
    return {};
  }
  value = std::string(first, last);
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string sha256File(const std::filesystem::path& path, std::string* error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    *error = "failed to open downloaded file for SHA-256 verification";
    return {};
  }

  turbo_crypto_sha256_ctx_t context{};
  if (turbo_crypto_sha256_init(&context) != TURBO_CRYPTO_OK) {
    *error = "failed to initialize SHA-256 verification";
    return {};
  }
  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (count > 0 &&
        turbo_crypto_sha256_update(&context, buffer.data(), static_cast<std::size_t>(count)) !=
            TURBO_CRYPTO_OK) {
      *error = "failed to update SHA-256 verification";
      return {};
    }
  }
  if (!input.eof()) {
    *error = "failed to read downloaded file for SHA-256 verification";
    return {};
  }

  std::array<unsigned char, TURBO_CRYPTO_SHA256_SIZE> digest{};
  if (turbo_crypto_sha256_final(&context, digest.data()) != TURBO_CRYPTO_OK) {
    *error = "failed to finalize SHA-256 verification";
    return {};
  }
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const auto byte : digest) {
    output << std::setw(2) << static_cast<unsigned int>(byte);
  }
  return output.str();
}

bool replaceFile(const std::filesystem::path& source,
                 const std::filesystem::path& destination,
                 std::string* error) {
#ifdef _WIN32
  if (!MoveFileExW(source.c_str(), destination.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    *error = "failed to atomically replace download destination: Windows error " +
             std::to_string(GetLastError());
    return false;
  }
  return true;
#else
  std::error_code ec;
  std::filesystem::rename(source, destination, ec);
  if (ec) {
    *error = "failed to atomically replace download destination: " + ec.message();
    return false;
  }
  return true;
#endif
}

TaskResult fail(std::string message) {
  TaskResult result(false, std::move(message));
  result.exit_code = 1;
  return result;
}

}  // namespace

TaskResult DownloadExecutor::execute(const Task& task, WorkflowContext& context) {
  const auto& params = std::get<DownloadParams>(task.specifics);
  const std::string url = substituteVariables(params.url, context);
  if (!isAbsoluteHttpsUrl(url)) {
    return fail("download.url must be an absolute HTTPS URL without whitespace");
  }

  const std::string requested_path = substituteVariables(params.path, context);
  if (requested_path.empty()) {
    return fail("download.path cannot be empty after variable substitution");
  }
  const auto destination =
      Praktor::util::resolveRelativePath(task.source_path, requested_path).lexically_normal();
  const auto temporary = std::filesystem::path(destination.string() + ".praktor.tmp");

  std::error_code ec;
  if (std::filesystem::exists(destination, ec) && !params.overwrite) {
    return fail("download destination already exists and overwrite is false: " +
                destination.string());
  }
  if (ec) {
    return fail("failed to inspect download destination: " + ec.message());
  }
  if (!destination.parent_path().empty()) {
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
      return fail("failed to create download directory: " + ec.message());
    }
  }
  std::filesystem::remove(temporary, ec);
  ec.clear();

  DownloadSink sink;
  sink.output.open(temporary, std::ios::binary | std::ios::trunc);
  if (!sink.output) {
    return fail("failed to open temporary download file: " + temporary.string());
  }

  turbo_http_options_t options{};
  if (turbo_http_options_init(&options, sizeof(options)) != TURBO_OK) {
    sink.output.close();
    std::filesystem::remove(temporary, ec);
    return fail("failed to initialize HTTPS download options");
  }
  options.timeout_ms = params.timeout_ms;

  turbo_http_t* http = nullptr;
  if (turbo_http_create_sync(&options, &http) != TURBO_OK || http == nullptr) {
    sink.output.close();
    std::filesystem::remove(temporary, ec);
    return fail("failed to initialize HTTPS download client");
  }

  http_response_t* response = turbo_http_request_stream_sync(
      http, HTTP_GET, url.c_str(), nullptr, 0, nullptr, 0, nullptr, writeDownloadChunk, &sink);
  sink.output.close();

  std::string error;
  if (response == nullptr) {
    error = "HTTPS download returned no response";
  } else if (response->error_code != HTTP_ERROR_NONE) {
    error = response->error != nullptr && response->error[0] != '\0'
                ? std::string("HTTPS download failed: ") + response->error
                : std::string("HTTPS download failed: ") +
                      http_error_to_str(response->error_code);
  } else if (response->status_code != 200) {
    error = "HTTPS download failed with status " + std::to_string(response->status_code);
  } else if (sink.failed) {
    error = "failed while writing the HTTPS response body";
  }
  if (response != nullptr) {
    http_response_free(response);
  }
  turbo_http_destroy(http);

  const std::string expected_sha256 = lowerTrim(substituteVariables(params.sha256, context));
  if (error.empty() && !expected_sha256.empty() &&
      (expected_sha256.size() != 64 ||
       !std::all_of(expected_sha256.begin(), expected_sha256.end(), [](unsigned char ch) {
         return std::isxdigit(ch) != 0;
       }))) {
    error = "download.sha256 must be a 64-character hexadecimal digest";
  }
  if (error.empty() && !expected_sha256.empty()) {
    const std::string actual_sha256 = sha256File(temporary, &error);
    if (error.empty() && actual_sha256 != expected_sha256) {
      error = "download SHA-256 mismatch";
    }
  }
  if (error.empty() && !replaceFile(temporary, destination, &error)) {
    // replaceFile provides the error.
  }
  if (!error.empty()) {
    std::filesystem::remove(temporary, ec);
    return fail(std::move(error));
  }

  context.setCurrentTaskOutput("path", destination.string());
  context.setCurrentTaskOutput("bytes", std::to_string(sink.bytes_written));
  context.setCurrentTaskOutput("sha256_verified", expected_sha256.empty() ? "false" : "true");
  return TaskResult(true);
}

std::unique_ptr<TaskExecutor> createDownloadExecutor() {
  return std::make_unique<DownloadExecutor>();
}

}  // namespace Praktor::Execution
