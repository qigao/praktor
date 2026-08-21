#include "system/managed_process.hpp"

#ifdef _WIN32

// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on

#include <algorithm>
#include <chrono>
#include <climits>
#include <cwctype>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Praktor::System {
namespace WinDetail {

ManagedProcessResult<bool> matchesManagedProcessCandidate(
    std::string_view configured_image_name,
    std::string_view canonical_image_name,
    std::uint32_t candidate_session_id,
    std::uint32_t current_session_id);
ManagedProcessResult<bool> matchesManagedProcessSnapshot(
    const ManagedProcessSnapshot& expected,
    const ManagedProcessSnapshot& observed,
    std::uint32_t current_session_id);

}  // namespace WinDetail
namespace {

constexpr DWORD kNonBlockingProbeTimeoutMs = 0;
constexpr DWORD kSessionMismatchExitCode = 1;
constexpr DWORD kMaximumProcessPathCharacters = 32768;

class UniqueHandle {
public:
  UniqueHandle() noexcept = default;
  explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
  ~UniqueHandle() noexcept { reset(); }

  UniqueHandle(const UniqueHandle&) = delete;
  UniqueHandle& operator=(const UniqueHandle&) = delete;

  UniqueHandle(UniqueHandle&& other) noexcept
      : handle_(std::exchange(other.handle_, nullptr)) {}

  UniqueHandle& operator=(UniqueHandle&& other) noexcept {
    if (this != &other) {
      reset(std::exchange(other.handle_, nullptr));
    }
    return *this;
  }

  HANDLE get() const noexcept { return handle_; }

  bool valid() const noexcept {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
  }

  void reset(HANDLE handle = nullptr) noexcept {
    if (valid()) {
      CloseHandle(handle_);
    }
    handle_ = handle;
  }

private:
  HANDLE handle_{nullptr};
};

template <typename T>
ManagedProcessResult<T> failure(int native_error, std::string message) {
  return {false, {}, native_error, std::move(message)};
}

ManagedProcessCommandResult commandFailure(int native_error,
                                           std::string message) {
  return {false, native_error, std::move(message)};
}

ManagedProcessCommandResult commandSuccess() { return {true, 0, {}}; }

ManagedProcessResult<std::wstring> wideFromUtf8(const std::string& value) {
  if (value.empty()) {
    return {true, {}, 0, {}};
  }
  const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                            value.c_str(), -1, nullptr, 0);
  if (required <= 0) {
    const int error = static_cast<int>(GetLastError());
    return failure<std::wstring>(error, "failed to decode UTF-8 process value");
  }
  std::wstring converted(static_cast<std::size_t>(required), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1,
                          converted.data(), required) <= 0) {
    const int error = static_cast<int>(GetLastError());
    return failure<std::wstring>(error, "failed to decode UTF-8 process value");
  }
  converted.pop_back();
  return {true, std::move(converted), 0, {}};
}

ManagedProcessResult<std::string> utf8FromWide(const std::wstring &value) {
  if (value.empty()) {
    return {true, {}, 0, {}};
  }
  const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(), -1,
                                           nullptr, 0, nullptr, nullptr);
  if (required <= 0) {
    const int error = static_cast<int>(GetLastError());
    return failure<std::string>(error, "failed to encode process path as UTF-8");
  }
  std::string converted(static_cast<std::size_t>(required), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(), -1, converted.data(),
                          required, nullptr, nullptr) <= 0) {
    const int error = static_cast<int>(GetLastError());
    return failure<std::string>(error, "failed to encode process path as UTF-8");
  }
  converted.pop_back();
  return {true, std::move(converted), 0, {}};
}

std::wstring trim(std::wstring value) {
  const auto first = std::find_if_not(value.begin(), value.end(),
                                      [](wchar_t ch) { return std::iswspace(ch) != 0; });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t ch) {
                      return std::iswspace(ch) != 0;
                    }).base();
  if (first >= last) {
    return {};
  }
  return {first, last};
}

ManagedProcessResult<bool> exactImageBasenameMatches(std::string_view configured_image_name,
                                                     std::string_view canonical_image_name) {
  auto configured = wideFromUtf8(std::string(configured_image_name));
  if (!configured.ok) {
    return failure<bool>(configured.native_error, std::move(configured.message));
  }
  auto canonical = wideFromUtf8(std::string(canonical_image_name));
  if (!canonical.ok) {
    return failure<bool>(canonical.native_error, std::move(canonical.message));
  }

  const auto configured_basename =
      std::filesystem::path(trim(configured.value)).filename().wstring();
  const auto canonical_basename = std::filesystem::path(trim(canonical.value)).filename().wstring();
  if (configured_basename.empty() || canonical_basename.empty()) {
    return failure<bool>(ERROR_INVALID_PARAMETER, "managed process image identity is empty");
  }
  if (configured_basename.size() > static_cast<std::size_t>(INT_MAX) ||
      canonical_basename.size() > static_cast<std::size_t>(INT_MAX)) {
    return failure<bool>(ERROR_INVALID_PARAMETER, "managed process image identity is too long");
  }

  SetLastError(ERROR_SUCCESS);
  const int comparison = CompareStringOrdinal(
      configured_basename.data(), static_cast<int>(configured_basename.size()),
      canonical_basename.data(), static_cast<int>(canonical_basename.size()), TRUE);
  if (comparison == 0) {
    return failure<bool>(static_cast<int>(GetLastError()),
                         "failed to compare managed process image identity");
  }
  return {true, comparison == CSTR_EQUAL, 0, {}};
}

std::uint64_t processInstanceToken(const FILETIME &creation_time) {
  ULARGE_INTEGER value{};
  value.LowPart = creation_time.dwLowDateTime;
  value.HighPart = creation_time.dwHighDateTime;
  return value.QuadPart;
}

ManagedProcessResult<ManagedProcessSnapshot> queryProcessEvidence(HANDLE process,
                                                                  std::uint32_t pid) {
  const DWORD wait_result = WaitForSingleObject(process, 0);
  if (wait_result == WAIT_OBJECT_0) {
    return {true, {}, 0, {}};
  }
  if (wait_result == WAIT_FAILED) {
    return failure<ManagedProcessSnapshot>(static_cast<int>(GetLastError()),
                                           "failed to observe managed process instance");
  }

  std::wstring full_path(kMaximumProcessPathCharacters, L'\0');
  DWORD full_path_size = static_cast<DWORD>(full_path.size());
  if (!QueryFullProcessImageNameW(process, 0, full_path.data(), &full_path_size)) {
    return failure<ManagedProcessSnapshot>(static_cast<int>(GetLastError()),
                                           "failed to query managed process image identity");
  }
  full_path.resize(full_path_size);
  auto canonical_image = utf8FromWide(std::filesystem::path(full_path).filename().wstring());
  if (!canonical_image.ok) {
    return failure<ManagedProcessSnapshot>(canonical_image.native_error,
                                           std::move(canonical_image.message));
  }

  DWORD session_id = 0;
  if (!ProcessIdToSessionId(static_cast<DWORD>(pid), &session_id)) {
    return failure<ManagedProcessSnapshot>(static_cast<int>(GetLastError()),
                                           "failed to query managed process session identity");
  }

  FILETIME creation_time{};
  FILETIME exit_time{};
  FILETIME kernel_time{};
  FILETIME user_time{};
  if (!GetProcessTimes(process, &creation_time, &exit_time, &kernel_time, &user_time)) {
    return failure<ManagedProcessSnapshot>(static_cast<int>(GetLastError()),
                                           "failed to query managed process instance token");
  }

  ManagedProcessSnapshot result;
  result.state = ManagedProcessState::Running;
  result.pid = pid;
  result.session_id = static_cast<std::uint32_t>(session_id);
  result.instance_token = processInstanceToken(creation_time);
  result.canonical_image_name = std::move(canonical_image.value);
  return {true, std::move(result), 0, {}};
}

std::wstring quoteWindowsArgument(const std::wstring& argument) {
  if (!argument.empty() &&
      argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    return argument;
  }

  std::wstring quoted = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t ch : argument) {
    if (ch == L'\\') {
      ++backslashes;
      continue;
    }
    if (ch == L'"') {
      quoted.append(backslashes * 2 + 1, L'\\');
      quoted.push_back(L'"');
      backslashes = 0;
      continue;
    }
    quoted.append(backslashes, L'\\');
    backslashes = 0;
    quoted.push_back(ch);
  }
  quoted.append(backslashes * 2, L'\\');
  quoted.push_back(L'"');
  return quoted;
}

struct CloseWindowContext {
  DWORD pid{0};
  bool matched{false};
  bool posted{false};
  DWORD first_error{ERROR_SUCCESS};
};

BOOL CALLBACK postCloseToProcessWindow(HWND window, LPARAM parameter) {
  auto* context = reinterpret_cast<CloseWindowContext*>(parameter);
  if (context == nullptr || !IsWindow(window)) {
    return TRUE;
  }

  DWORD window_pid = 0;
  GetWindowThreadProcessId(window, &window_pid);
  if (window_pid != context->pid || GetWindow(window, GW_OWNER) != nullptr) {
    return TRUE;
  }

  context->matched = true;
  if (PostMessageW(window, WM_CLOSE, 0, 0)) {
    context->posted = true;
  } else if (context->first_error == ERROR_SUCCESS) {
    context->first_error = GetLastError();
  }
  return TRUE;
}

struct ValidatedProcess {
  UniqueHandle process;
  ManagedProcessSnapshot evidence;
};

ManagedProcessResult<ValidatedProcess> openValidatedProcess(const ManagedProcessSnapshot &snapshot,
                                                            DWORD action_access) {
  if (snapshot.state != ManagedProcessState::Running || snapshot.pid == 0 ||
      snapshot.instance_token == 0 || snapshot.canonical_image_name.empty()) {
    return failure<ValidatedProcess>(ERROR_INVALID_PARAMETER,
                                     "managed process snapshot lacks stable identity evidence");
  }

  DWORD current_session_id = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &current_session_id)) {
    return failure<ValidatedProcess>(static_cast<int>(GetLastError()),
                                     "failed to determine current process session");
  }

  UniqueHandle process(OpenProcess(action_access | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                   FALSE, static_cast<DWORD>(snapshot.pid)));
  if (!process.valid()) {
    return failure<ValidatedProcess>(static_cast<int>(GetLastError()),
                                     "failed to open managed process instance for action");
  }

  auto evidence = queryProcessEvidence(process.get(), snapshot.pid);
  if (!evidence.ok) {
    return failure<ValidatedProcess>(evidence.native_error, std::move(evidence.message));
  }
  if (evidence.value.state != ManagedProcessState::Running) {
    return failure<ValidatedProcess>(ERROR_NOT_FOUND,
                                     "managed process instance is no longer running");
  }

  auto identity_match = WinDetail::matchesManagedProcessSnapshot(
      snapshot, evidence.value, static_cast<std::uint32_t>(current_session_id));
  if (!identity_match.ok) {
    return failure<ValidatedProcess>(identity_match.native_error,
                                     std::move(identity_match.message));
  }
  if (!identity_match.value) {
    return failure<ValidatedProcess>(ERROR_INVALID_DATA,
                                     "managed process instance identity changed before action");
  }

  ValidatedProcess validated;
  validated.process = std::move(process);
  validated.evidence = std::move(evidence.value);
  return {true, std::move(validated), 0, {}};
}

class WindowsManagedProcessBackend final : public IManagedProcessBackend {
public:
  ManagedProcessResult<ManagedProcessSnapshot> query(
      const ManagedProcessIdentity& identity) override {
    if (identity.image_name.empty()) {
      return failure<ManagedProcessSnapshot>(ERROR_INVALID_PARAMETER,
                                             "managed process identity is empty");
    }

    DWORD current_session_id = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &current_session_id)) {
      return failure<ManagedProcessSnapshot>(
          static_cast<int>(GetLastError()),
          "failed to determine current process session");
    }

    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) {
      return failure<ManagedProcessSnapshot>(
          static_cast<int>(GetLastError()),
          "failed to create managed process snapshot");
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) {
      const DWORD error = GetLastError();
      if (error == ERROR_NO_MORE_FILES) {
        return {true, {}, 0, {}};
      }
      return failure<ManagedProcessSnapshot>(
          static_cast<int>(error), "failed to enumerate managed processes");
    }

    do {
      auto entry_image = utf8FromWide(entry.szExeFile);
      if (!entry_image.ok) {
        return failure<ManagedProcessSnapshot>(entry_image.native_error,
                                               std::move(entry_image.message));
      }
      auto name_match = WinDetail::matchesManagedProcessCandidate(
          identity.image_name, entry_image.value,
          static_cast<std::uint32_t>(current_session_id),
          static_cast<std::uint32_t>(current_session_id));
      if (!name_match.ok) {
        return failure<ManagedProcessSnapshot>(name_match.native_error,
                                               std::move(name_match.message));
      }
      if (!name_match.value) {
        continue;
      }

      DWORD candidate_session_id = 0;
      if (!ProcessIdToSessionId(entry.th32ProcessID, &candidate_session_id)) {
        return failure<ManagedProcessSnapshot>(
            static_cast<int>(GetLastError()),
            "failed to query managed process candidate session");
      }
      if (candidate_session_id != current_session_id) {
        continue;
      }

      UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                                           SYNCHRONIZE,
                                       FALSE, entry.th32ProcessID));
      if (!process.valid()) {
        return failure<ManagedProcessSnapshot>(
            static_cast<int>(GetLastError()),
            "failed to open managed process candidate");
      }
      auto evidence = queryProcessEvidence(
          process.get(), static_cast<std::uint32_t>(entry.th32ProcessID));
      if (!evidence.ok) {
        return evidence;
      }
      if (evidence.value.state == ManagedProcessState::NotRunning) {
        continue;
      }
      auto evidence_match = WinDetail::matchesManagedProcessCandidate(
          identity.image_name, evidence.value.canonical_image_name,
          evidence.value.session_id,
          static_cast<std::uint32_t>(current_session_id));
      if (!evidence_match.ok) {
        return failure<ManagedProcessSnapshot>(evidence_match.native_error,
                                               std::move(evidence_match.message));
      }
      if (evidence_match.value) {
        return evidence;
      }
    } while (Process32NextW(snapshot.get(), &entry));

    const DWORD error = GetLastError();
    if (error != ERROR_NO_MORE_FILES) {
      return failure<ManagedProcessSnapshot>(
          static_cast<int>(error), "failed to enumerate managed processes");
    }
    return {true, {}, 0, {}};
  }

  ManagedProcessResult<std::uint32_t> start(
      const ManagedProcessParams& params) override {
    auto executable = wideFromUtf8(params.executable);
    if (!executable.ok) {
      return failure<std::uint32_t>(executable.native_error,
                                    std::move(executable.message));
    }
    auto working_directory = wideFromUtf8(params.working_directory);
    if (!working_directory.ok) {
      return failure<std::uint32_t>(working_directory.native_error,
                                    std::move(working_directory.message));
    }

    std::wstring command_line = quoteWindowsArgument(executable.value);
    for (const auto& argument : params.arguments) {
      auto wide_argument = wideFromUtf8(argument);
      if (!wide_argument.ok) {
        return failure<std::uint32_t>(wide_argument.native_error,
                                      std::move(wide_argument.message));
      }
      command_line.push_back(L' ');
      command_line += quoteWindowsArgument(wide_argument.value);
    }

    if (working_directory.value.empty()) {
      working_directory.value =
          std::filesystem::path(executable.value).parent_path().wstring();
    }

    DWORD current_session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &current_session)) {
      const int error = static_cast<int>(GetLastError());
      return failure<std::uint32_t>(
          error, "failed to determine current process session");
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION process_info{};
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');
    const wchar_t* current_directory = working_directory.value.empty()
                                           ? nullptr
                                           : working_directory.value.c_str();

    if (!CreateProcessW(executable.value.c_str(), mutable_command.data(), nullptr,
                        nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        current_directory, &startup, &process_info)) {
      const int error = static_cast<int>(GetLastError());
      return failure<std::uint32_t>(error, "failed to start managed process");
    }

    UniqueHandle process(process_info.hProcess);
    UniqueHandle thread(process_info.hThread);
    DWORD observed_session = 0;
    const BOOL session_observed =
        ProcessIdToSessionId(process_info.dwProcessId, &observed_session);
    const DWORD observation_error =
        session_observed ? ERROR_SUCCESS : GetLastError();
    if (!session_observed || observed_session != current_session) {
      const DWORD error = session_observed ? ERROR_INVALID_DATA
                                           : observation_error;
      static_cast<void>(TerminateProcess(process.get(), kSessionMismatchExitCode));
      static_cast<void>(WaitForSingleObject(process.get(), kNonBlockingProbeTimeoutMs));
      return failure<std::uint32_t>(
          static_cast<int>(error),
          "managed process started in an unexpected session");
    }

    const DWORD wait_result =
        WaitForSingleObject(process.get(), kNonBlockingProbeTimeoutMs);
    if (wait_result == WAIT_OBJECT_0) {
      return failure<std::uint32_t>(ERROR_PROCESS_ABORTED,
                                    "managed process exited during startup");
    }
    if (wait_result == WAIT_FAILED) {
      const int error = static_cast<int>(GetLastError());
      return failure<std::uint32_t>(
          error, "failed to observe managed process startup");
    }
    return {true, static_cast<std::uint32_t>(process_info.dwProcessId), 0, {}};
  }

  ManagedProcessCommandResult requestStop(const ManagedProcessSnapshot &snapshot) override {
    auto validated = openValidatedProcess(snapshot, 0);
    if (!validated.ok) {
      return commandFailure(validated.native_error, std::move(validated.message));
    }

    CloseWindowContext context;
    context.pid = static_cast<DWORD>(validated.value.evidence.pid);
    SetLastError(ERROR_SUCCESS);
    if (!EnumWindows(postCloseToProcessWindow, reinterpret_cast<LPARAM>(&context))) {
      const DWORD native_error = GetLastError();
      const int error =
          static_cast<int>(native_error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : native_error);
      return commandFailure(error, "failed to enumerate managed process windows");
    }
    if (context.matched && !context.posted) {
      const DWORD native_error =
          context.first_error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : context.first_error;
      return commandFailure(static_cast<int>(native_error),
                            "failed to request managed process close");
    }
    return commandSuccess();
  }

  ManagedProcessCommandResult terminate(const ManagedProcessSnapshot &snapshot) override {
    auto validated = openValidatedProcess(snapshot, PROCESS_TERMINATE);
    if (!validated.ok) {
      return commandFailure(validated.native_error, std::move(validated.message));
    }
    if (!TerminateProcess(validated.value.process.get(), 1)) {
      const DWORD terminate_error = GetLastError();
      const DWORD wait_result =
          WaitForSingleObject(validated.value.process.get(), 0);
      if (wait_result == WAIT_OBJECT_0) {
        return commandSuccess();
      }
      if (wait_result == WAIT_FAILED) {
        return commandFailure(static_cast<int>(GetLastError()),
                              "failed to observe managed process termination");
      }
      const int error = static_cast<int>(terminate_error);
      return commandFailure(error, "failed to terminate managed process");
    }
    return commandSuccess();
  }
};

}  // namespace

std::unique_ptr<IManagedProcessBackend> createManagedProcessBackend() {
  return std::make_unique<WindowsManagedProcessBackend>();
}

namespace WinDetail {

ManagedProcessResult<bool> matchesManagedProcessCandidate(std::string_view configured_image_name,
                                                          std::string_view canonical_image_name,
                                                          std::uint32_t candidate_session_id,
                                                          std::uint32_t current_session_id) {
  if (candidate_session_id != current_session_id) {
    return {true, false, 0, {}};
  }
  return exactImageBasenameMatches(configured_image_name, canonical_image_name);
}

ManagedProcessResult<bool> matchesManagedProcessSnapshot(const ManagedProcessSnapshot &expected,
                                                         const ManagedProcessSnapshot &observed,
                                                         std::uint32_t current_session_id) {
  if (expected.state != ManagedProcessState::Running ||
      observed.state != ManagedProcessState::Running || expected.pid == 0 ||
      expected.pid != observed.pid || expected.session_id != observed.session_id ||
      observed.session_id != current_session_id || expected.instance_token == 0 ||
      expected.instance_token != observed.instance_token) {
    return {true, false, 0, {}};
  }
  return exactImageBasenameMatches(expected.canonical_image_name, observed.canonical_image_name);
}

} // namespace WinDetail

}  // namespace Praktor::System

#else

namespace Praktor::System {
namespace {

template <typename T>
ManagedProcessResult<T> unsupportedResult() {
  return {false, {}, 0, std::string(kManagedProcessUnsupportedPlatform)};
}

class UnsupportedManagedProcessBackend final : public IManagedProcessBackend {
public:
  ManagedProcessResult<ManagedProcessSnapshot> query(
      const ManagedProcessIdentity&) override {
    return unsupportedResult<ManagedProcessSnapshot>();
  }

  ManagedProcessResult<std::uint32_t> start(
      const ManagedProcessParams&) override {
    return unsupportedResult<std::uint32_t>();
  }

  ManagedProcessCommandResult requestStop(
      const ManagedProcessSnapshot&) override {
    return {false, 0, std::string(kManagedProcessUnsupportedPlatform)};
  }

  ManagedProcessCommandResult terminate(
      const ManagedProcessSnapshot&) override {
    return {false, 0, std::string(kManagedProcessUnsupportedPlatform)};
  }
};

}  // namespace

std::unique_ptr<IManagedProcessBackend> createManagedProcessBackend() {
  return std::make_unique<UnsupportedManagedProcessBackend>();
}

}  // namespace Praktor::System

#endif
