#include "dag/trigger_executor.hpp"
#include "executors/command_executor.hpp"
#include "fmtlog.h"
#include "util/variable_substitution.hpp"

#include <fstream>
#include <sstream>
#include <variant>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <curl/curl.h>
#endif

namespace Praktor {
namespace Execution {

namespace {
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                // Control char -> skip or encode; keep simple
            } else {
                out += ch;
            }
        }
    }
    return out;
}

#ifdef _WIN32
// Windows HTTP implementation using WinHTTP
bool sendHttpPostWindows(const std::string& url,
                        const std::string& body,
                        const std::unordered_map<std::string, std::string>& headers)
{
    // Parse URL
    std::wstring wurl(url.begin(), url.end());
    URL_COMPONENTS urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostname[256] = {0};
    wchar_t path[1024] = {0};
    urlComp.lpszHostName = hostname;
    urlComp.dwHostNameLength = sizeof(hostname) / sizeof(wchar_t);
    urlComp.lpszUrlPath = path;
    urlComp.dwUrlPathLength = sizeof(path) / sizeof(wchar_t);

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &urlComp)) {
        loge("Failed to parse URL: {}", url);
        return false;
    }

    HINTERNET hSession = WinHttpOpen(L"Praktor/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS,
                                     0);
    if (!hSession) {
        loge("WinHttpOpen failed");
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, hostname, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        loge("WinHttpConnect failed");
        return false;
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect,
                                           L"POST",
                                           path,
                                           NULL,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        loge("WinHttpOpenRequest failed");
        return false;
    }

    // Add headers
    for (const auto& [key, value] : headers) {
        std::wstring header = std::wstring(key.begin(), key.end()) + L": " +
                             std::wstring(value.begin(), value.end());
        WinHttpAddRequestHeaders(hRequest, header.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    }

    // Send request
    bool success = WinHttpSendRequest(hRequest,
                                     WINHTTP_NO_ADDITIONAL_HEADERS,
                                     0,
                                     (LPVOID)body.c_str(),
                                     body.length(),
                                     body.length(),
                                     0) &&
                  WinHttpReceiveResponse(hRequest, NULL);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return success;
}
#else
// Unix/Linux HTTP implementation using libcurl
size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    (void)contents;
    (void)userp;
    return size * nmemb;
}

bool sendHttpPostCurl(const std::string& url,
                     const std::string& body,
                     const std::unordered_map<std::string, std::string>& headers)
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        loge("Failed to initialize CURL");
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.length());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    struct curl_slist* header_list = nullptr;
    for (const auto& [key, value] : headers) {
        std::string header = key + ": " + value;
        header_list = curl_slist_append(header_list, header.c_str());
    }
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    CURLcode res = curl_easy_perform(curl);
    bool success = (res == CURLE_OK);

    if (!success) {
        loge("HTTP POST failed: {}", curl_easy_strerror(res));
    }

    if (header_list) {
        curl_slist_free_all(header_list);
    }
    curl_easy_cleanup(curl);

    return success;
}
#endif

bool sendHttpPost(const std::string& url,
                 const std::string& body,
                 const std::unordered_map<std::string, std::string>& headers)
{
#ifdef _WIN32
    return sendHttpPostWindows(url, body, headers);
#else
    return sendHttpPostCurl(url, body, headers);
#endif
}

} // anonymous namespace

void TriggerExecutor::executeTriggers(const Task& task,
                                     bool success,
                                     WorkflowContext& context,
                                     const std::unordered_map<std::string, std::string>& environment)
{
    if (!task.triggers || task.triggers->empty()) {
        return;
    }

    const auto& triggers = *task.triggers;
    std::vector<TriggerAction> actions_to_execute;

    // Collect actions based on task status
    if (success) {
        actions_to_execute.insert(actions_to_execute.end(),
                                 triggers.on_success.begin(),
                                 triggers.on_success.end());
    } else {
        actions_to_execute.insert(actions_to_execute.end(),
                                 triggers.on_failure.begin(),
                                 triggers.on_failure.end());
    }

    // on_complete always executes
    actions_to_execute.insert(actions_to_execute.end(),
                             triggers.on_complete.begin(),
                             triggers.on_complete.end());

    // Execute all collected actions
    for (const auto& action : actions_to_execute) {
        try {
            executeTriggerAction(action, context, environment);
        } catch (const std::exception& e) {
            logw("Trigger execution failed: {}", e.what());
        }
    }
}

void TriggerExecutor::executeTriggerAction(const TriggerAction& action,
                                          WorkflowContext& context,
                                          const std::unordered_map<std::string, std::string>& environment)
{
    std::visit([this, &context, &environment](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, HttpPostTrigger>) {
            executeHttpPost(arg, context, environment);
        } else if constexpr (std::is_same_v<T, WriteFileTrigger>) {
            executeWriteFile(arg, context);
        } else if constexpr (std::is_same_v<T, RunTaskTrigger>) {
            executeRunTask(arg, context, environment);
        } else if constexpr (std::is_same_v<T, PraktorNotifyTrigger>) {
            executePraktorNotify(arg, context, environment);
        }
    }, action);
}

void TriggerExecutor::executeHttpPost(const HttpPostTrigger& trigger,
                                     WorkflowContext& context,
                                     const std::unordered_map<std::string, std::string>& environment)
{
    (void)environment; // May be used for future enhancements

    std::string url = substituteVariables(trigger.url, context);
    std::string body = trigger.body ? substituteVariables(*trigger.body, context) : "";

    std::unordered_map<std::string, std::string> headers;
    for (const auto& [key, value] : trigger.headers) {
        headers[key] = substituteVariables(value, context);
    }

    // Add default Content-Type if not specified
    if (headers.find("Content-Type") == headers.end()) {
        headers["Content-Type"] = "application/json";
    }

    logi("Executing HTTP POST trigger to: {}", url);

    if (sendHttpPost(url, body, headers)) {
        logi("HTTP POST trigger completed successfully");
    } else {
        logw("HTTP POST trigger failed");
    }
}

void TriggerExecutor::executeWriteFile(const WriteFileTrigger& trigger,
                                      WorkflowContext& context)
{
    std::string path_str = substituteVariables(trigger.path, context);
    std::string content = substituteVariables(trigger.content, context);

    logi("Executing write_file trigger: {}", path_str);

    try {
        std::filesystem::path fs_path(path_str);
        if (fs_path.has_parent_path()) {
            std::filesystem::create_directories(fs_path.parent_path());
        }

        std::ios_base::openmode mode = std::ios::out;
        if (trigger.mode == WriteFileMode::Append) {
            mode |= std::ios::app;
        }

        std::ofstream file(fs_path, mode);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + path_str);
        }

        file << content;
        file.close();

        logi("write_file trigger completed successfully");
    } catch (const std::exception& e) {
        loge("write_file trigger failed: {}", e.what());
        throw;
    }
}

void TriggerExecutor::executeRunTask(const RunTaskTrigger& trigger,
                                    WorkflowContext& context,
                                    const std::unordered_map<std::string, std::string>& environment)
{
    std::string task_name = substituteVariables(trigger.task_name, context);

    logi("Executing run_task trigger: {}", task_name);

    // Create a synthetic task for the trigger
    Task synthetic_task;
    synthetic_task.name = task_name + "_trigger";
    synthetic_task.action = TaskAction::RunCommand;

    RunCommandParams params;
    params.command = task_name;
    params.environment = environment;
    synthetic_task.specifics = params;

    try {
        CommandExecutor executor;
        TaskExecutor* executor_ptr = &executor;
        auto result = executor_ptr->execute(synthetic_task, context);

        if (result.success) {
            logi("run_task trigger completed successfully");
        } else {
            logw("run_task trigger failed: {}", result.error_message);
        }
    } catch (const std::exception& e) {
        loge("run_task trigger failed: {}", e.what());
        throw;
    }
}


void TriggerExecutor::executePraktorNotify(const PraktorNotifyTrigger& trigger,
                                         WorkflowContext& context,
                                         const std::unordered_map<std::string, std::string>& environment)
{
    std::string message = substituteVariables(trigger.message, context);

    // Prefer explicit PRAKTOR_WEBHOOK, fallback to PRAKTOR_NOTIFY_URL
    std::string webhook;
    auto it = environment.find("PRAKTOR_WEBHOOK");
    if (it != environment.end()) {
        webhook = it->second;
    } else {
        it = environment.find("PRAKTOR_NOTIFY_URL");
        if (it != environment.end()) webhook = it->second;
    }

    if (webhook.empty()) {
        logi("@praktor {}", message);
        return;
    }

    HttpPostTrigger http;
    http.url = webhook;
    http.body = std::string("{\"text\": \"") + json_escape(message) + "\"}";
    http.headers["Content-Type"] = "application/json";

    executeHttpPost(http, context, environment);
}

} // namespace Execution
} // namespace Praktor

