#include "praktor/process_driver.h"
#include <praktor/process_retainer.h>
#include <core/clock.h>
#include <threads/threads.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct driver_state {
  process_async process;
  praktor_process_retainer *retainer;
  praktor_process_retainer_token reservation;
  bool reserved, closing, done, failed, limited, terminal;
  bool input_pending, input_closed, pending[2], eof[2];
  size_t input_size, input_offset, input_chunk, capacity, size[2], line_size[2];
  char input_buffer[4096], read_buffer[2][4096];
  char *input, *output[2], *line[2];
  process_result_t result;
  praktor_process_emit emit;
  void *emit_user;
  char error[192];
} driver_state;

static driver_state *state_of(const praktor_process_driver *d) {
  return d == NULL ? NULL : (driver_state *)d->impl;
}
static void fail(driver_state *s, const char *operation, int error) {
  s->failed = true;
  if (s->error[0] == '\0')
    (void)snprintf(s->error, sizeof(s->error), "Salts::NativeIO %s failed (status %d)", operation, error);
}
static bool peer_closed(int error) {
  if (error == SALTS_EPIPE) return true;
#if defined(_WIN32)
  /* Win32 ERROR_BROKEN_PIPE / ERROR_NO_DATA; no OS headers or OS calls. */
  return error == -109 || error == -232;
#else
  return error == -EPIPE;
#endif
}
static void emit_bytes(driver_state *s, size_t stream, const char *bytes, size_t size) {
  size_t i;
  if (s->emit == NULL) return;
  for (i = 0u; i < size; ++i) {
    if (bytes[i] == '\r') continue;
    if (bytes[i] == '\n') {
      if (s->emit(s->emit_user, s->line[stream], s->line_size[stream]) != SALTS_OK) {
        fail(s, "stream callback", SALTS_EIO); return;
      }
      s->line_size[stream] = 0u;
    } else {
      if (s->line_size[stream] == s->capacity) {
        fail(s, "stream line capacity", SALTS_ENOBUFS); return;
      }
      s->line[stream][s->line_size[stream]++] = bytes[i];
    }
  }
}
static void completion(void *user, uint64_t request,
                       process_stream stream, const native_io_completion *event) {
  driver_state *s = (driver_state *)user;
  size_t index, remaining, accepted;
  (void)request;
  if (s == NULL || event == NULL) return;
  if (stream == SALTS_PROCESS_STDIN) s->input_pending = false;
  else if (stream == SALTS_PROCESS_STDOUT) s->pending[0] = false;
  else if (stream == SALTS_PROCESS_STDERR) s->pending[1] = false;
  else { fail(s, "completion stream", SALTS_EINVAL); return; }
  /* Late close/retainer acknowledgements touch only this C payload. */
  if (s->closing) return;
  if (stream == SALTS_PROCESS_STDIN) {
    if (event->kind == NATIVE_IO_COMPLETION_OK) {
      if (event->bytes == 0u || event->bytes > s->input_chunk)
        fail(s, "stdin progress", SALTS_EIO);
      else s->input_offset += event->bytes;
    } else if (event->kind == NATIVE_IO_COMPLETION_EOF ||
               (event->kind == NATIVE_IO_COMPLETION_FAILED && peer_closed(event->status))) {
      s->input_offset = s->input_size;
    } else fail(s, "stdin I/O", event->status);
    return;
  }
  index = stream == SALTS_PROCESS_STDOUT ? 0u : 1u;
  if (event->kind == NATIVE_IO_COMPLETION_EOF) { s->eof[index] = true; return; }
  if (event->kind != NATIVE_IO_COMPLETION_OK || event->bytes > sizeof(s->read_buffer[index])) {
    fail(s, "read I/O", event->status); return;
  }
  remaining = s->capacity - s->size[0] - s->size[1];
  accepted = event->bytes < remaining ? event->bytes : remaining;
  memcpy(s->output[index] + s->size[index], s->read_buffer[index], accepted);
  s->size[index] += accepted;
  s->output[index][s->size[index]] = '\0';
  emit_bytes(s, index, s->read_buffer[index], accepted);
  if (accepted != event->bytes) {
    s->limited = true;
    if (s->error[0] == '\0')
      (void)snprintf(s->error, sizeof(s->error), "Captured process output exceeded the configured limit");
  }
}
static void free_state(void *payload) {
  driver_state *s = (driver_state *)payload;
  free(s->input); free(s->output[0]); free(s->output[1]);
  free(s->line[0]); free(s->line[1]); free(s);
}
int praktor_process_driver_start(praktor_process_driver *driver,
    const process_options_t *options, const void *input, size_t input_size,
    size_t max_output_bytes, struct praktor_process_retainer *retainer,
    praktor_process_emit emit, void *emit_user) {
  driver_state *s;
  process_async_config config = {0};
  int result;
  if (driver == NULL || driver->impl != NULL || options == NULL ||
      (input_size != 0u && input == NULL) || input_size == SIZE_MAX ||
      max_output_bytes == 0u || max_output_bytes > (SIZE_MAX - 1u) / 4u)
    return SALTS_EINVAL;
  s = (driver_state *)calloc(1u, sizeof(*s));
  if (s == NULL) return SALTS_ENOMEM;
  s->input = (char *)malloc(input_size + 1u);
  s->output[0] = (char *)calloc(max_output_bytes + 1u, 1u);
  s->output[1] = (char *)calloc(max_output_bytes + 1u, 1u);
  if (emit != NULL) {
    s->line[0] = (char *)malloc(max_output_bytes + 1u);
    s->line[1] = (char *)malloc(max_output_bytes + 1u);
  }
  if (s->input == NULL || s->output[0] == NULL || s->output[1] == NULL ||
      (emit != NULL && (s->line[0] == NULL || s->line[1] == NULL))) {
    free_state(s); return SALTS_ENOMEM;
  }
  if (input_size != 0u) memcpy(s->input, input, input_size);
  s->input[input_size] = '\0'; s->input_size = input_size;
  s->capacity = max_output_bytes; s->emit = emit; s->emit_user = emit_user;
  s->result.exit_code = -1; s->result.pid = -1;
  s->retainer = retainer;
  if (retainer != NULL) {
    result = praktor_process_retainer_reserve(retainer, &s->reservation);
    if (result != SALTS_OK) { free_state(s); return result; }
    s->reserved = true;
  }
#if defined(_WIN32)
  config.backend_kind = NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
  config.backend_kind = NATIVE_IO_BACKEND_EPOLL;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
  config.backend_kind = NATIVE_IO_BACKEND_KQUEUE;
#else
  if (s->reserved) (void)praktor_process_retainer_release(retainer, s->reservation);
  free_state(s); return SALTS_ENOTSUP;
#endif
  config.request_capacity = 8u;
  config.completion_batch_capacity = 8u;
  config.completion = completion; config.completion_user = s;
  driver->impl = s;
  result = process_async_start(&s->process, options, &config);
  if (result != SALTS_OK) {
    fail(s, "start", result); s->done = true;
    /* A failed provider admission may still require close/destroy. Retain the
     * handle until the caller uses the same close/release path. */
  }
  return result;
}
static void submit_read(driver_state *s, size_t index) {
  uint64_t request = 0u;
  int result;
  if (s->pending[index] || s->eof[index]) return;
  result = index == 0u
      ? process_async_read_stdout(&s->process, s->read_buffer[index], sizeof(s->read_buffer[index]), &request)
      : process_async_read_stderr(&s->process, s->read_buffer[index], sizeof(s->read_buffer[index]), &request);
  if (result == SALTS_OK) s->pending[index] = true;
  else if (result != SALTS_ENOBUFS) fail(s, "read submission", result);
}
static void submit_input(driver_state *s) {
  if (s->input_pending || s->input_closed) return;
  if (!s->terminal && s->input_offset < s->input_size) {
    uint64_t request = 0u;
    int result;
    s->input_chunk = s->input_size - s->input_offset;
    if (s->input_chunk > sizeof(s->input_buffer)) s->input_chunk = sizeof(s->input_buffer);
    memcpy(s->input_buffer, s->input + s->input_offset, s->input_chunk);
    result = process_async_write_stdin(&s->process, s->input_buffer, s->input_chunk, &request);
    if (result == SALTS_OK) s->input_pending = true;
    else if (result != SALTS_ENOBUFS) fail(s, "stdin submission", result);
  } else {
    int result = process_async_close_stdin(&s->process);
    if (result == SALTS_OK) s->input_closed = true;
    else if (result != SALTS_EBUSY) fail(s, "stdin close", result);
  }
}
int praktor_process_driver_tick(praktor_process_driver *driver, bool *done) {
  driver_state *s = state_of(driver);
  size_t pass;
  if (s == NULL || done == NULL) return SALTS_EINVAL;
  if (s->closing) return SALTS_EBUSY;
  if (s->done) { *done = true; return SALTS_OK; }
  for (pass = 0u; pass < 2u && !s->failed && !s->limited; ++pass) {
    size_t progressed = 0u;
    int r;
    submit_read(s, 0u); submit_read(s, 1u); submit_input(s);
    r = process_async_run_ready(&s->process, 64u, &progressed);
    if (r != SALTS_OK) fail(s, "driver", r);
    r = process_async_poll(&s->process, &s->result);
    if (r == SALTS_OK) s->terminal = true;
    else if (r != SALTS_EBUSY) fail(s, "poll", r);
  }
  s->done = s->failed || s->limited ||
      (s->terminal && s->eof[0] && s->eof[1] && s->input_closed &&
       !s->input_pending && !s->pending[0] && !s->pending[1]);
  if (s->done && s->emit != NULL && !s->failed) {
    size_t i;
    for (i = 0u; i < 2u; ++i) {
      if (s->line_size[i] != 0u && s->emit(s->emit_user, s->line[i], s->line_size[i]) != SALTS_OK)
        fail(s, "stream callback", SALTS_EIO);
      s->line_size[i] = 0u;
    }
  }
  *done = s->done;
  return SALTS_OK;
}
int praktor_process_driver_terminate(praktor_process_driver *driver) {
  driver_state *s = state_of(driver);
  if (s == NULL || s->closing) return SALTS_EINVAL;
  return process_async_terminate(&s->process);
}
int praktor_process_driver_view(const praktor_process_driver *driver, praktor_process_view *view) {
  const driver_state *s = state_of(driver);
  if (s == NULL || view == NULL) return SALTS_EINVAL;
  view->active = s->process.impl != NULL; view->closing = s->closing;
  view->done = s->done; view->failed = s->failed; view->output_limited = s->limited;
  view->result = s->result; view->stdout_data = s->output[0]; view->stderr_data = s->output[1];
  view->stdout_size = s->size[0]; view->stderr_size = s->size[1]; view->error = s->error;
  return SALTS_OK;
}
int praktor_process_driver_close(praktor_process_driver *driver, uint32_t budget_ms) {
  driver_state *s = state_of(driver);
  uint64_t started, budget;
  size_t attempt;
  if (s == NULL) return SALTS_EINVAL;
  s->closing = true; s->emit = NULL; s->emit_user = NULL;
  started = hrtime(); budget = (uint64_t)budget_ms * UINT64_C(1000000);
  if (s->process.impl != NULL) {
    int r = process_async_close(&s->process);
    if (r != SALTS_OK && r != SALTS_EBUSY) fail(s, "close", r);
    for (attempt = 0u; attempt < 4096u && hrtime() - started < budget; ++attempt) {
      size_t progressed = 0u; bool quiescent = false;
      r = process_async_run_ready(&s->process, 64u, &progressed);
      quiescent = process_async_is_quiescent(&s->process);
      if (r != SALTS_OK && r != SALTS_EBUSY) { fail(s, "shutdown step", r); break; }
      if (quiescent && process_async_is_quiescent(&s->process)) {
        r = process_async_destroy(&s->process);
        if (r != SALTS_OK) fail(s, "destroy", r);
        break;
      }
      if (progressed == 0u) sleep_ms(1u);
    }
    if (s->process.impl != NULL) {
      if (s->error[0] == '\0')
        (void)snprintf(s->error, sizeof(s->error), "Salts::NativeIO still owns active I/O leases");
      return SALTS_EBUSY;
    }
  }
  return s->failed ? SALTS_EIO : SALTS_OK;
}
int praktor_process_driver_release(praktor_process_driver *driver) {
  driver_state *s = state_of(driver);
  if (s == NULL) return SALTS_EINVAL;
  int r;
  if (s->process.impl != NULL) {
    if (!s->closing || !s->reserved || s->retainer == NULL) return SALTS_EBUSY;
    r = praktor_process_retainer_adopt(s->retainer, s->reservation, &s->process, s, free_state);
    if (r != SALTS_OK) return r;
    driver->impl = NULL;
    return SALTS_OK;
  }
  if (s->reserved) {
    r = praktor_process_retainer_release(s->retainer, s->reservation);
    if (r != SALTS_OK) return r;
  }
  driver->impl = NULL; free_state(s);
  return SALTS_OK;
}

int praktor_process_driver_run(praktor_process_driver *driver,
                               int (*cancelled)(void *user), void *user) {
  driver_state *s = state_of(driver);
  bool terminated = false, done = false;
  if (s == NULL) return SALTS_EINVAL;
  while (!done) {
    int r;
    if (!terminated && cancelled != NULL && cancelled(user)) {
      terminated = true;
      r = praktor_process_driver_terminate(driver);
      if (r != SALTS_OK) fail(s, "terminate", r);
    }
    r = praktor_process_driver_tick(driver, &done);
    if (r != SALTS_OK) return r;
    if (!done) sleep_ms(1u);
  }
  return s->failed ? SALTS_EIO : SALTS_OK;
}
int praktor_process_driver_drain(praktor_process_driver *driver) {
  driver_state *s = state_of(driver);
  if (s == NULL) return SALTS_EINVAL;
  do {
    (void)praktor_process_driver_close(driver, 20u);
    if (s->process.impl != NULL) sleep_ms(1u);
  } while (s->process.impl != NULL);
  return s->failed ? SALTS_EIO : SALTS_OK;
}
