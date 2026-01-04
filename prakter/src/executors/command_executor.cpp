#ifdef _WIN32
  #define _WINSOCKAPI_
  #include <WinSock2.h>
  #include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <unordered_map>
#include <variant>

#include <uv.h>

#include "executors/command_executor.hpp"
#include "fmtlog.h"
#include "util/variable_substitution.hpp"

namespace Prakter::Execution
{

// Helper structures for libuv process execution (copied from original)
struct ProcessResult
{
  int64_t exit_code = 0;
  std::string stdout_data;
  std::string stderr_data;
};

struct ProcessContext
{
  uv_process_t process;
  uv_process_options_t options;
  uv_pipe_t stdout_pipe;
  uv_pipe_t stderr_pipe;
  ProcessResult result;
  std::vector<char*> args_ptr;
  std::vector<char*> env_ptr;
};

// libuv callbacks (copied from original)
// Forward declarations for close callbacks
void on_handle_closed(uv_handle_t* handle);

void on_process_exit(uv_process_t* req, int64_t exit_status, int term_signal)
{
  ProcessContext* context = (ProcessContext*)req->data;
  context->result.exit_code = exit_status;
  uv_close((uv_handle_t*)req, on_handle_closed);
}

void on_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
  *buf = uv_buf_init((char*)malloc(suggested_size), suggested_size);
}

void on_stdout_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
  ProcessContext* context = (ProcessContext*)stream->data;
  if (nread > 0) {
    context->result.stdout_data.append(buf->base, nread);
  } else if (nread < 0) {
    uv_close((uv_handle_t*)stream, on_handle_closed);
  }
  free(buf->base);
}

void on_stderr_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
  ProcessContext* context = (ProcessContext*)stream->data;
  if (nread > 0) {
    context->result.stderr_data.append(buf->base, nread);
  } else if (nread < 0) {
    uv_close((uv_handle_t*)stream, on_handle_closed);
  }
  free(buf->base);
}

void on_handle_closed(uv_handle_t* handle) {
    ProcessContext* context = (ProcessContext*)handle->data;
    // Check if all relevant handles are closed
    // This is a simplified check; a more robust solution might track each handle type
    // For now, we assume closing any of the three (process, stdout, stderr) means we're done with that part.
    // We need to ensure the loop stops only when all are truly done.
    // A simple way is to stop the loop after the process exits and its pipes are closed.
    // This callback is called for each handle. We need a way to know when all are closed.
    // For simplicity, we'll rely on uv_run(loop, UV_RUN_DEFAULT) to exit when no active handles remain.
    // However, to be explicit, we can stop the loop once the process itself has exited and its pipes are closed.
    // This requires a more sophisticated state tracking.
    // For now, let's ensure uv_loop_close is called after uv_run.
}

TaskResult CommandExecutor::execute(const Task& task,
                                       WorkflowContext& context)
{
  logi("Executing run_command: {}", task.name);

  // Extract RunCommandParams from task.specifics
  try {
    auto params = std::get<RunCommandParams>(task.specifics);

    // Override working_directory with task-level working_dir if specified
    if (task.working_dir.has_value() && !task.working_dir->empty()) {
      params.working_directory = *task.working_dir;
    }

    TaskResult result = executeProcess(params, context);

    if (result.success) {
      applyOutputs(task, result, context);
    }

    return result;
  } catch (const std::bad_variant_access& e) {
    return TaskResult(
        false,
        fmt::format("Task does not contain RunCommandParams: {}", e.what()));
  } catch (const std::exception& e) {
    return TaskResult(false,
                      fmt::format("RunCommand execution failed: {}", e.what()));
  }
}

TaskResult CommandExecutor::executeProcess(const RunCommandParams& params,
                                              const WorkflowContext& context)
{
  uv_loop_t loop_instance;
  uv_loop_init(&loop_instance);
  ProcessContext proc_context;
  proc_context.process.data = &proc_context; // Set data for process handle
  proc_context.stdout_pipe.data = &proc_context; // Set data for stdout pipe
  proc_context.stderr_pipe.data = &proc_context; // Set data for stderr pipe

  // 1. Prepare arguments (copied and simplified from original)
  std::vector<std::string> command_parts;
  if (std::holds_alternative<std::string>(params.command)) {
    std::string full_command =
        substituteVariables(std::get<std::string>(params.command), context);
#ifdef _WIN32
    command_parts.push_back("cmd");
    command_parts.push_back("/c");
    command_parts.push_back(full_command);
#else
    command_parts.push_back("/bin/sh");
    command_parts.push_back("-c");
    command_parts.push_back(full_command);
#endif
  } else {
    auto cmdList = std::get<StrList>(params.command);
    for (const auto& part : cmdList) {
      command_parts.push_back(substituteVariables(part, context));
    }
  }

  proc_context.args_ptr.reserve(command_parts.size() + 1);
  for (auto& part : command_parts) {
    proc_context.args_ptr.push_back(&part[0]);
  }
  proc_context.args_ptr.push_back(NULL);

  // 2. Prepare environment variables
  std::vector<std::string> env_vars;
  if (!params.environment.empty()) {
    std::unordered_map<std::string, std::string> merged_env;
    uv_env_item_t* env_items = nullptr;
    int env_count = 0;
    if (uv_os_environ(&env_items, &env_count) == 0) {
      for (int i = 0; i < env_count; ++i) {
        merged_env.emplace(env_items[i].name, env_items[i].value);
      }
      uv_os_free_environ(env_items, env_count);
    }
    for (const auto& [key, value] : params.environment) {
      merged_env[key] = substituteVariables(value, context);
    }
    env_vars.reserve(merged_env.size());
    proc_context.env_ptr.reserve(merged_env.size() + 1);
    for (auto& [key, value] : merged_env) {
      env_vars.push_back(key + "=" + value);
    }
    for (auto& var : env_vars) {
      proc_context.env_ptr.push_back(var.data());
    }
    proc_context.env_ptr.push_back(nullptr);
  }

  // 3. Setup options
  proc_context.options = {0};
  proc_context.options.exit_cb = on_process_exit;
  proc_context.options.file = proc_context.args_ptr[0];
  proc_context.options.args = proc_context.args_ptr.data();
  proc_context.options.env =
      params.environment.empty() ? nullptr : proc_context.env_ptr.data();
#ifdef _WIN32
  proc_context.options.flags |= UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS;
#endif

  std::string cwd_param = substituteVariables(params.working_directory, context);
  std::string actual_cwd = cwd_param.empty() ? std::filesystem::current_path().string() : cwd_param;
  proc_context.options.cwd = actual_cwd.c_str();

  if (std::holds_alternative<std::string>(params.command)) {
    std::string full_command = substituteVariables(std::get<std::string>(params.command), context);
    logd("Command: {} (CWD: {})", full_command, actual_cwd);
  } else {
    if (!command_parts.empty()) {
        logd("Command (list): {} (CWD: {})", command_parts[0], actual_cwd);
    } else {
        logd("Command (list): <empty> (CWD: {})", actual_cwd);
    }
  }

  // 4. Setup stdio redirection
  uv_pipe_init(&loop_instance, &proc_context.stdout_pipe, 0);
  uv_pipe_init(&loop_instance, &proc_context.stderr_pipe, 0);
  uv_stdio_container_t stdio[3];
  stdio[0].flags = UV_IGNORE;
  stdio[1].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
  stdio[1].data.stream = (uv_stream_t*)&proc_context.stdout_pipe;
  stdio[2].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
  stdio[2].data.stream = (uv_stream_t*)&proc_context.stderr_pipe;
  proc_context.options.stdio = stdio;
  proc_context.options.stdio_count = 3;

  // 5. Spawn process
  int r = uv_spawn(&loop_instance, &proc_context.process, &proc_context.options);
  if (r != 0) {
    uv_loop_close(&loop_instance); // Clean up loop on spawn failure
    return TaskResult(
        false, "Failed to spawn process: " + std::string(uv_strerror(r)));
  }

  uv_read_start(
      (uv_stream_t*)&proc_context.stdout_pipe, on_alloc, on_stdout_read);
  uv_read_start(
      (uv_stream_t*)&proc_context.stderr_pipe, on_alloc, on_stderr_read);

  // 6. Run the event loop
  uv_run(&loop_instance, UV_RUN_DEFAULT);

  // Ensure all handles are closed before closing the loop.
  // The on_process_exit and on_stdout/stderr_read callbacks will close their respective handles.
  // uv_run(loop, UV_RUN_DEFAULT) will block until all active handles are closed.
  // After uv_run returns, we can safely close the loop.
  uv_loop_close(&loop_instance);

  // 7. Create result
  TaskResult result(proc_context.result.exit_code == 0);
  result.exit_code = proc_context.result.exit_code;
  result.stdout_data = proc_context.result.stdout_data;
  result.stderr_data = proc_context.result.stderr_data;

  if (!result.success) {
    result.error_message = fmt::format("Command failed with exit code: {}. Stderr: {}", 
        result.exit_code, result.stderr_data);
    loge("Command failed. Stdout: {} Stderr: {}", result.stdout_data, result.stderr_data);
  } else {
    logd("Command succeeded. Stdout: {} Stderr: {}", result.stdout_data, result.stderr_data);
  }

  return result;
}

void CommandExecutor::applyOutputs(const Task& task,
                                        const TaskResult& result,
                                        WorkflowContext& context)
  {
    jsoncons::json outputs = jsoncons::json::object();
    outputs["exit_code"] = result.exit_code;
    if (!result.stdout_data.empty()) {
      outputs["stdout"] = result.stdout_data;
    }
    if (!result.stderr_data.empty()) {
      outputs["stderr"] = result.stderr_data;
    }

    if (std::holds_alternative<RunCommandParams>(task.specifics)) {
      const auto& params = std::get<RunCommandParams>(task.specifics);
      if (params.output_format == CommandOutputFormat::Json && !result.stdout_data.empty()) {
        try {
          outputs["data"] = jsoncons::json::parse(result.stdout_data);
        } catch (const jsoncons::json_exception& e) {
          throw std::runtime_error("Failed to parse stdout as JSON for '" + task.name + "': " + e.what());
        }
      }
    }

    for (const auto& item : outputs.object_range()) {
      context.setCurrentTaskOutput(item.key(), item.value());
    }
  }


std::unique_ptr<TaskExecutor> createCommandExecutor()
{
  return std::make_unique<CommandExecutor>();
}

}  // namespace Prakter::Execution
