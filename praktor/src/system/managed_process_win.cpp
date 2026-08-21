#include "system/managed_process.hpp"

#ifdef _WIN32

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Praktor::System {
namespace {

constexpr DWORD kStartupProbeTimeoutMs = 500;
constexpr DWORD kSessionMismatchExitCode = 1;

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

std::wstring lower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
    return static_cast<wchar_t>(std::towlower(ch));
  });
  return value;
}

std::wstring trim(std::wstring value) {
  const auto first = std::find_if_not(value.begin(), value.end(), [](wchar_t ch) {
    return std::iswspace(ch) != 0;
  });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t ch) {
                      return std::iswspace(ch) != 0;
                    }).base();
  if (first >= last) {
    return {};
  }
  return {first, last};
}

std::vector<std::wstring> processNameCandidates(
    const std::wstring& configured_name) {
  std::vector<std::wstring> candidates;
  const std::wstring configured = trim(configured_name);
  if (configured.empty()) {
    return candidates;
  }
  candidates.push_back(
      lower(std::filesystem::path(configured).filename().wstring()));
  const std::wstring stem =
      lower(std::filesystem::path(configured).stem().wstring());
  if (!stem.empty() &&
      std::find(candidates.begin(), candidates.end(), stem) == candidates.end()) {
    candidates.push_back(stem);
  }
  return candidates;
}

bool matchesConfiguredName(const std::wstring& executable_name,
                           const std::vector<std::wstring>& candidates) {
  const std::wstring executable = lower(
      std::filesystem::path(executable_name).filename().wstring());
  const std::wstring stem =
      lower(std::filesystem::path(executable).stem().wstring());
  return std::find(candidates.begin(), candidates.end(), executable) !=
             candidates.end() ||
         std::find(candidates.begin(), candidates.end(), stem) !=
             candidates.end();
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

class WindowsManagedProcessBackend final : public IManagedProcessBackend {
public:
  ManagedProcessResult<ManagedProcessSnapshot> query(
      const ManagedProcessIdentity& identity) override {
    auto wide_name = wideFromUtf8(identity.image_name);
    if (!wide_name.ok) {
      return failure<ManagedProcessSnapshot>(wide_name.native_error,
                                             std::move(wide_name.message));
    }
    const auto candidates = processNameCandidates(wide_name.value);
    if (candidates.empty()) {
      return failure<ManagedProcessSnapshot>(ERROR_INVALID_PARAMETER,
                                             "managed process identity is empty");
    }

    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) {
      const int error = static_cast<int>(GetLastError());
      return failure<ManagedProcessSnapshot>(
          error, "failed to create managed process snapshot");
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) {
      const DWORD error = GetLastError();
      if (error == ERROR_NO_MORE_FILES) {
        return {true, {ManagedProcessState::NotRunning, 0}, 0, {}};
      }
      return failure<ManagedProcessSnapshot>(
          static_cast<int>(error), "failed to enumerate managed processes");
    }

    do {
      if (matchesConfiguredName(entry.szExeFile, candidates)) {
        return {true,
                {ManagedProcessState::Running,
                 static_cast<std::uint32_t>(entry.th32ProcessID)},
                0,
                {}};
      }
    } while (Process32NextW(snapshot.get(), &entry));

    const DWORD error = GetLastError();
    if (error != ERROR_NO_MORE_FILES) {
      return failure<ManagedProcessSnapshot>(
          static_cast<int>(error), "failed to enumerate managed processes");
    }
    return {true, {ManagedProcessState::NotRunning, 0}, 0, {}};
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
      static_cast<void>(WaitForSingleObject(process.get(), kStartupProbeTimeoutMs));
      return failure<std::uint32_t>(
          static_cast<int>(error),
          "managed process started in an unexpected session");
    }

    const DWORD wait_result =
        WaitForSingleObject(process.get(), kStartupProbeTimeoutMs);
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

  ManagedProcessCommandResult requestStop(
      const ManagedProcessSnapshot& snapshot) override {
    if (snapshot.state != ManagedProcessState::Running || snapshot.pid == 0) {
      return commandFailure(ERROR_INVALID_PARAMETER,
                            "managed process snapshot is not running");
    }

    CloseWindowContext context;
    context.pid = static_cast<DWORD>(snapshot.pid);
    SetLastError(ERROR_SUCCESS);
    if (!EnumWindows(postCloseToProcessWindow,
                     reinterpret_cast<LPARAM>(&context))) {
      const DWORD native_error = GetLastError();
      const int error = static_cast<int>(
          native_error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : native_error);
      return commandFailure(error, "failed to enumerate managed process windows");
    }
    if (context.matched && !context.posted) {
      const DWORD native_error = context.first_error == ERROR_SUCCESS
                                     ? ERROR_GEN_FAILURE
                                     : context.first_error;
      return commandFailure(static_cast<int>(native_error),
                            "failed to request managed process close");
    }
    return commandSuccess();
  }

  ManagedProcessCommandResult terminate(
      const ManagedProcessSnapshot& snapshot) override {
    if (snapshot.state != ManagedProcessState::Running || snapshot.pid == 0) {
      return commandFailure(ERROR_INVALID_PARAMETER,
                            "managed process snapshot is not running");
    }

    UniqueHandle process(OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE,
                                     static_cast<DWORD>(snapshot.pid)));
    if (!process.valid()) {
      const DWORD error = GetLastError();
      if (error == ERROR_INVALID_PARAMETER) {
        return commandSuccess();
      }
      return commandFailure(static_cast<int>(error),
                            "failed to open managed process for termination");
    }
    if (!TerminateProcess(process.get(), 1)) {
      const int error = static_cast<int>(GetLastError());
      return commandFailure(error, "failed to terminate managed process");
    }
    return commandSuccess();
  }
};

}  // namespace

std::unique_ptr<IManagedProcessBackend> createManagedProcessBackend() {
  return std::make_unique<WindowsManagedProcessBackend>();
}

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
