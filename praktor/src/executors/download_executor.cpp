#include "executors/download_executor.hpp"
#include "download_executor_internal.hpp"

#include "util/path_utils.hpp"
#include "util/variable_substitution.hpp"

#include <http_client/http.h>
#include <cmeta_crypto.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
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

struct DownloadUrl {
  std::string connection_uri;
  std::string authority;
  std::string target;
};

bool isAbsoluteHttpsUrl(std::string_view url) {
  constexpr std::string_view prefix = "https://";
  if (!url.starts_with(prefix) || url.size() == prefix.size()) {
    return false;
  }
  return std::none_of(url.begin(), url.end(), [](unsigned char ch) {
    return std::iscntrl(ch) != 0 || std::isspace(ch) != 0;
  });
}

bool parseDownloadUrl(std::string_view url, DownloadUrl* result) {
  constexpr std::string_view prefix = "https://";
  if (result == nullptr || !isAbsoluteHttpsUrl(url)) {
    return false;
  }
  const std::string_view remainder = url.substr(prefix.size());
  const std::size_t path_start = remainder.find('/');
  result->authority = std::string(remainder.substr(0, path_start));
  result->target = path_start == std::string_view::npos
                       ? "/"
                       : std::string(remainder.substr(path_start));
  if (result->authority.empty() || result->authority.find('@') != std::string::npos) {
    return false;
  }
  const bool bracketed_ipv6 = result->authority.front() == '[';
  const bool has_port = bracketed_ipv6
                            ? result->authority.find("]:" ) != std::string::npos
                            : result->authority.find(':') != std::string::npos;
  result->connection_uri = "tls://" + result->authority + (has_port ? "" : ":443");
  return true;
}

chttp_client_config downloadClientConfig(std::uint32_t timeout_ms) {
  chttp_client_config config{};
#if defined(_WIN32)
  config.network.backend = NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
  config.network.backend = NATIVE_IO_BACKEND_EPOLL;
#else
  config.network.backend = NATIVE_IO_BACKEND_KQUEUE;
#endif
  config.network.connection_capacity = 1u;
  config.network.command_capacity = 8u;
  config.network.request_capacity = 2u;
  config.network.completion_batch_capacity = 8u;
  config.network.event_capacity = 16u;
  config.network.max_send_bytes = 64u * 1024u;
  config.network.receive_buffer_bytes = 64u * 1024u;
  config.network.connect_timeout_ms = timeout_ms;
  config.network.read_timeout_ms = timeout_ms;
  config.network.write_timeout_ms = timeout_ms;
  config.request_capacity = 1u;
  config.max_start_line_bytes = 8u * 1024u;
  config.max_header_count = 100u;
  config.max_header_bytes = 64u * 1024u;
  config.max_response_body_bytes = 0u;
  config.max_informational_responses = 4u;
  config.stream_chunk_bytes = 64u * 1024u;
  return config;
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

}  // namespace

std::string Internal::sha256File(const std::filesystem::path& path, std::string* error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    *error = "failed to open downloaded file for SHA-256 verification";
    return {};
  }

  cmeta_sha256_stream* stream = nullptr;
  if (cmeta_sha256_stream_create(&stream) != SALTS_OK) {
    *error = "failed to initialize SHA-256 verification";
    return {};
  }
  const std::unique_ptr<cmeta_sha256_stream, decltype(&cmeta_sha256_stream_destroy)>
      owner(stream, &cmeta_sha256_stream_destroy);
  constexpr std::size_t kChecksumChunkBytes = 64u * 1024u;
  std::array<char, kChecksumChunkBytes> bytes;
  for (;;) {
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto count = input.gcount();
    if (count > 0 &&
        cmeta_sha256_stream_update(stream, bytes.data(), static_cast<std::size_t>(count)) != SALTS_OK) {
      *error = "failed to calculate SHA-256 verification";
      return {};
    }
    if (input.bad() || (input.fail() && !input.eof())) {
      *error = "failed to read downloaded file for SHA-256 verification";
      return {};
    }
    if (input.eof()) break;
  }
  std::array<std::uint8_t, SALTS_SHA256_DIGEST_BYTES> digest{};
  if (cmeta_sha256_stream_finish(stream, digest.data()) != SALTS_OK) {
    *error = "failed to calculate SHA-256 verification";
    return {};
  }
  constexpr char kHexDigits[] = "0123456789abcdef";
  std::string result(digest.size() * 2, '0');
  for (std::size_t index = 0; index < digest.size(); ++index) {
    result[index * 2] = kHexDigits[digest[index] >> 4];
    result[index * 2 + 1] = kHexDigits[digest[index] & 0x0f];
  }
  return result;
}

namespace {

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

  DownloadUrl parsed_url;
  if (!parseDownloadUrl(url, &parsed_url)) {
    return fail("download.url must contain a valid HTTPS authority");
  }
  chttp_client client{};
  const chttp_client_config client_config = downloadClientConfig(params.timeout_ms);
  if (chttp_client_init(&client, &client_config) != SALTS_OK) {
    return fail("failed to initialize HTTPS download client");
  }
  chttp_options options{};
  options.connection_uri = parsed_url.connection_uri.c_str();
  options.authority = parsed_url.authority.c_str();
  options.target = parsed_url.target.c_str();
  options.timeout_ms = params.timeout_ms;
  chttp_response response{};
  chttp_error response_error{};
  const int request_status = chttp_download_file(
      &client, &options, temporary.string().c_str(), nullptr, nullptr, &response, &response_error);
  std::string error;
  if (request_status != SALTS_OK) {
    error = response_error.stage != nullptr
                ? std::string("HTTPS download failed at ") + response_error.stage
                : "HTTPS download failed";
  } else if (response.status_code != 200) {
    error = "HTTPS download failed with status " + std::to_string(response.status_code);
  }
  chttp_response_destroy(&response);
  if (chttp_client_destroy(&client, params.timeout_ms) != SALTS_OK && error.empty()) {
    error = "failed to shut down HTTPS download client";
  }

  const std::string expected_sha256 = lowerTrim(substituteVariables(params.sha256, context));
  if (error.empty() && !expected_sha256.empty() &&
      (expected_sha256.size() != 64 ||
       !std::all_of(expected_sha256.begin(), expected_sha256.end(), [](unsigned char ch) {
         return std::isxdigit(ch) != 0;
       }))) {
    error = "download.sha256 must be a 64-character hexadecimal digest";
  }
  if (error.empty() && !expected_sha256.empty()) {
    const std::string actual_sha256 = Internal::sha256File(temporary, &error);
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
  context.setCurrentTaskOutput("bytes", std::to_string(std::filesystem::file_size(destination, ec)));
  context.setCurrentTaskOutput("sha256_verified", expected_sha256.empty() ? "false" : "true");
  return TaskResult(true);
}

std::unique_ptr<TaskExecutor> createDownloadExecutor() {
  return std::make_unique<DownloadExecutor>();
}

}  // namespace Praktor::Execution
