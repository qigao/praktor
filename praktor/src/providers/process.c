#include "providers/process.h"
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
  char error[256];
} process_job;
static process_job *owned_job(void *self, praktor_service_job *handle) {
  process_job *job = handle ? handle->impl : NULL;
  return job && job->owner == self ? job : NULL;
}
static int start(void *self, const praktor_process_request *request, praktor_service_job *out) {
  praktor_process_service *service = self;
  if (!service || !service->active || !request || !out || out->impl ||
      request->count > PRAKTOR_PROCESS_MAX_ARGS || (request->count && !request->args))
    return SALTS_EINVAL;
  size_t bytes = 0u;
  for (size_t i = 0u; i < request->count; ++i) {
    if (!request->args[i])
      return SALTS_EINVAL;
    size_t n = strlen(request->args[i]);
    if (n >= PRAKTOR_PROCESS_MAX_ARG_BYTES - bytes)
      return SALTS_ENOBUFS;
    bytes += n + 1u;
  }
  if (request->args && request->args[request->count])
    return SALTS_EINVAL;
  if (service->jobs >= PRAKTOR_PROCESS_MAX_JOBS)
    return SALTS_EBUSY;
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
  options.args = request->args;
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
static int poll(void *self, praktor_service_job *handle, praktor_service_result *out) {
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
  *out = (praktor_service_result){job->done,
                                  job->success && !job->cancelled,
                                  job->cancelled,
                                  job->exit_code,
                                  view.stdout_data ? view.stdout_data : "",
                                  view.stderr_data ? view.stderr_data : "",
                                  job->error};
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
CMETA_IMPLEMENTS(praktor_execution_service, process_service, 0u, .start = start, .poll = poll,
                 .cancel = cancel, .release = release);
praktor_execution_service praktor_process_service_bind(praktor_process_service *service) {
  return praktor_execution_service_bind(service, &process_service_vtable);
}
