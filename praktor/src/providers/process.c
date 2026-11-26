#include "providers/process.h"
#include "actions/variable_reference.h"
#include <cmeta/data_reflect.h>
#include <core/error_codes.h>
#include <core/fmt.h>
#include <praktor/process_driver.h>
#include <stdlib.h>
#include <string.h>
/* Read-only native storage projection. Paths remain explicit borrows from the
 * declaration owner; reflection does not grant string ownership/lifecycle. */
cmeta_reflect_data(praktor_process_config, "praktor.process.config", cmeta_field(int, timeout_ms));
cmeta_reflect_data(praktor_process_service, "praktor.process.service",
                   cmeta_field(int, timeout_ms));
const cmeta_data_desc *praktor_process_config_reflection(void) {
  return cmeta_reflected_data(praktor_process_config);
}
const cmeta_data_desc *praktor_process_service_reflection(void) {
  return cmeta_reflected_data(praktor_process_service);
}
typedef struct process_job {
  praktor_process_service *owner;
  praktor_process_driver driver;
  bool closing, done, success, cancelled;
  int exit_code;
  char exit_text[32];
  praktor_execution_output outputs[3];
  char error[256];
} process_job;
int praktor_process_validate_inputs(const json_value_t *args,
                                    const praktor_execution_admission *admission) {
  if (!admission) return SALTS_EINVAL;
  const json_value_t *inputs = admission->inputs;
  char *error = admission->error;
  size_t capacity = admission->error_capacity;
  if (!args || json_type(args) != JSON_ARRAY || json_array_size(args) > PRAKTOR_PROCESS_MAX_ARGS ||
      (inputs && json_type(inputs) != JSON_OBJECT))
    return SALTS_EINVAL;
  for (size_t i = 0u; i < json_array_size(args); ++i) {
    const char *text = json_string(json_array_get(args, i));
    if (!text)
      return SALTS_EINVAL;
    const vstr argument = vstr_from_cstr(text);
    const vstr key = praktor_variable_reference(argument);
    if (praktor_retired_variable_reference(argument)) {
      if (error && capacity) fmt_text(error, capacity, "Use $name for a tool input reference");
      return SALTS_EINVAL;
    }
    if (key.data && !json_string(json_object_get_v(inputs, key))) {
      if (error && capacity)
        fmt(error, capacity, "missing tool input {}", text);
      return SALTS_EINVAL;
    }
  }
  /* No silently ignored invocation fields: every supplied input must bind an
   * entire argv entry. Repeated references deliberately reuse the same value. */
  for (size_t i = 0u; i < json_object_size(inputs); ++i) {
    const char *key = json_object_key(inputs, i);
    const json_value_t *item = json_object_value(inputs,i);
    const char *value = json_string(item);
    if (!key || !*key || strlen(key) != json_object_key_len(inputs,i) ||
        (value && strlen(value) != json_string_len(item))) {
      if (error && capacity) fmt_text(error, capacity, "Tool inputs must be NUL-free text");
      return SALTS_EINVAL;
    }
    if (!value || praktor_retired_variable_reference(vstr_from_cstr(value))) {
      if (error && capacity) fmt_text(error, capacity, "Use $name for a tool value reference");
      return SALTS_EINVAL;
    }
    bool used = false;
    for (size_t j = 0u; j < json_array_size(args); ++j) {
      const char *text = json_string(json_array_get(args, j));
      const vstr input = praktor_variable_reference(vstr_from_cstr(text));
      if (input.data && vstr_eq(input, vstr_from_cstr(key))) {
        used = true;
        break;
      }
    }
    if (!used) {
      if (error && capacity)
        fmt(error, capacity, "unused tool input '{}'", key);
      return SALTS_EINVAL;
    }
  }
  return SALTS_OK;
}
static int admit(void *self, const praktor_execution_admission *admission) {
  const praktor_process_service *service = self;
  return service && service->active ?
      praktor_process_validate_inputs(service->args, admission) : SALTS_EINVAL;
}
static process_job *owned_job(void *self, praktor_service_job *handle) {
  process_job *job = handle ? handle->impl : NULL;
  return job && job->owner == self ? job : NULL;
}
static int start_process(praktor_process_service *service, const char *const *args,
                         praktor_service_job *out) {
  process_job *job = calloc(1u, sizeof(*job));
  if (!job)
    return SALTS_ENOMEM;
  job->owner = service;
  job->exit_code = -1;
  process_options_t options;
  process_options_init(&options);
  /* NativeIO owns all three stdio pipes; synchronous capture flags conflict
   * with process_async admission. Executable argv is passed without a shell. */
  options.flags = 0u;
  options.program = service->path;
  options.args = args;
  options.cwd = service->working_dir;
  options.timeout_ms = (uint64_t)service->timeout_ms;
  int rc = praktor_process_driver_start(&job->driver, &options, NULL, 0u,
                                        PRAKTOR_PROCESS_MAX_OUTPUT, NULL, NULL, NULL);
  if (rc != SALTS_OK) {
    job->closing = true;
    fmt(job->error, sizeof job->error, "process admission failed ({})", rc);
  }
  /* Failed admission may still retain native IO. Keep its cleanup in the
   * accepted job rather than abandoning a half-admitted process. */
  ++service->jobs;
  out->impl = job;
  return SALTS_OK;
}
static int start(void *self, const praktor_execution_request *request, praktor_service_job *out) {
  praktor_process_service *service = self;
  if (!service || !service->active || !request || !out || out->impl)
    return SALTS_EINVAL;
  if (service->jobs >= PRAKTOR_PROCESS_MAX_JOBS)
    return SALTS_EBUSY;

  const size_t count = json_array_size(service->args);
  const char *argv[PRAKTOR_PROCESS_MAX_ARGS + 1u] = {0};
  tstr resolved[PRAKTOR_PROCESS_MAX_ARGS] = {0};
  size_t bytes = 0u;
  int status = SALTS_OK;
  for (size_t i = 0u; i < count; ++i) {
    const char *argument = json_string(json_array_get(service->args, i));
    const vstr key = praktor_variable_reference(vstr_from_cstr(argument));
    if (key.data) {
      argument = json_string(json_object_get_v(request->inputs, key));
      if (!argument || !request->resolve_text) {
        status = SALTS_EINVAL;
        break;
      }
      status = request->resolve_text(request->user, argument, PRAKTOR_PROCESS_MAX_OUTPUT, &resolved[i]);
      if (status != SALTS_OK)
        break;
      argument = resolved[i];
    }
    const size_t length = strlen(argument);
    if (length >= PRAKTOR_PROCESS_MAX_ARG_BYTES - bytes) {
      status = SALTS_ENOBUFS;
      break;
    }
    bytes += length + 1u;
    argv[i] = argument;
  }
  if (status == SALTS_OK)
    status = start_process(service, argv, out);
  for (size_t i = 0u; i < count; ++i)
    tstr_free(resolved[i]);
  return status;
}

static int poll(void *self, praktor_service_job *handle, praktor_execution_result *out) {
  process_job *job = owned_job(self, handle);
  if (!job || !out)
    return SALTS_EINVAL;
  praktor_process_view view = {0};
  if (!job->done) {
    if (job->driver.impl && !job->closing) {
      bool done = false;
      int rc = praktor_process_driver_tick(&job->driver, &done);
      if (done || rc != SALTS_OK) {
        (void)praktor_process_driver_view(&job->driver, &view);
        job->exit_code = view.result.exit_code;
        job->success = rc == SALTS_OK && !view.failed && !view.output_limited &&
                       view.result.state == SALTS_PROCESS_EXITED && view.result.exit_code == 0;
        if (view.output_limited)
          fmt_text(job->error, sizeof job->error, "tool output exceeded its capture limit");
        if ((view.stdout_size && memchr(view.stdout_data, '\0', view.stdout_size)) ||
            (view.stderr_size && memchr(view.stderr_data, '\0', view.stderr_size))) {
          job->success = false;
          fmt_text(job->error, sizeof job->error, "tool output must be NUL-free text");
        }
        if (!job->success) {
          if (!job->error[0]) {
            if (view.stderr_size)
              fmt_text(job->error, sizeof job->error, view.stderr_data);
            else
              fmt(job->error, sizeof job->error, "process failed (state {}, exit {})",
                  (int)view.result.state, view.result.exit_code);
          }
        }
        job->closing = true;
      }
    }
    if (job->closing) {
      if (job->driver.impl) {
        (void)praktor_process_driver_close(&job->driver, 1u);
        (void)praktor_process_driver_view(&job->driver, &view);
        if (view.failed) {
          job->success = false;
          if (!job->error[0])
            fmt_text(job->error, sizeof job->error, view.error);
        }
        job->done = !view.active;
      } else
        job->done = true;
    }
  }
  if (job->driver.impl)
    (void)praktor_process_driver_view(&job->driver, &view);
  const bool success = job->success && !job->cancelled;
  size_t output_count = 0u;
  if (job->done) {
    const int length = fmt(job->exit_text, sizeof job->exit_text, "{}", job->exit_code);
    if (length <= 0 || (size_t)length >= sizeof job->exit_text)
      return SALTS_EINVAL;
    job->outputs[output_count++] = (praktor_execution_output){"shell_exit_code", job->exit_text};
    if (success) {
      job->outputs[output_count++] = (praktor_execution_output){
          "shell_output", view.stdout_data ? view.stdout_data : ""};
      job->outputs[output_count++] = (praktor_execution_output){
          "shell_stderr", view.stderr_data ? view.stderr_data : ""};
    }
  }
  *out = (praktor_execution_result){.done = job->done, .success = success, .cancelled = job->cancelled,
                                   .outputs = output_count ? job->outputs : NULL,
                                   .output_count = output_count, .error = job->error};
  return SALTS_OK;
}
static int cancel(void *self, praktor_service_job *handle) {
  process_job *job = owned_job(self, handle);
  if (!job)
    return SALTS_EINVAL;
  if (job->done || job->cancelled)
    return SALTS_OK;
  job->cancelled = true;
  job->success = false;
  job->closing = true;
  if (job->driver.impl)
    (void)praktor_process_driver_close(&job->driver, 0u);
  return SALTS_OK;
}
static int release(void *self, praktor_service_job *handle) {
  process_job *job = owned_job(self, handle);
  if (!job)
    return SALTS_EINVAL;
  if (!job->done)
    return SALTS_EBUSY;
  if (job->driver.impl) {
    int rc = praktor_process_driver_release(&job->driver);
    if (rc != SALTS_OK)
      return rc;
  }
  --job->owner->jobs;
  free(job);
  handle->impl = NULL;
  return SALTS_OK;
}
CMETA_IMPLEMENTS(praktor_execution_service, process_service, 0u, .admit = admit, .start = start, .poll = poll,
                 .cancel = cancel, .release = release);
praktor_execution_service praktor_process_service_bind(praktor_process_service *service) {
  return praktor_execution_service_bind(service, &process_service_vtable);
}
