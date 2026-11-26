#ifndef PRAKTOR_EXECUTION_SERVICE_H
#define PRAKTOR_EXECUTION_SERVICE_H

#include "praktor.h"
#include <cmeta/interface.h>
#include <core/tstr.h>
#include <json_parser.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct praktor_execution_admission {
  const json_value_t *inputs;
  char *error;
  size_t error_capacity;
} praktor_execution_admission;

typedef struct praktor_execution_request {
  const json_value_t *inputs;
  /* Resolves a literal or a task-scope reference into owned text. A provider
   * frees successful results with tstr_free. Only callable during start. */
  int (*resolve_text)(void *user, const char *input, size_t max_reference_bytes, tstr *out);
  void *user;
} praktor_execution_request;

typedef struct praktor_service_job {
  void *impl;
} praktor_service_job;

typedef struct praktor_execution_output {
  const char *key, *text;
} praktor_execution_output;

typedef struct praktor_execution_result {
  bool done, success, cancelled;
  /* Terminal values are provider-owned NUL-free text, borrowed until release.
   * A failed job may publish diagnostic ports alongside its error. */
  const praktor_execution_output *outputs;
  size_t output_count;
  const char *error;
  /* A provider can terminate workflow execution instead of returning a
   * recoverable BT failure. Zero preserves ordinary action semantics. */
  praktor_runtime_status terminal_status;
} praktor_execution_result;

/* All operations run on the workflow owner. Admission has no job side effects.
 * start accepts admitted inputs and borrows its request for the call. Success
 * returns an owned job, including a backend failure with resources to drain.
 * Cancellation is idempotent and release requires a terminal poll result. */
#define PRAKTOR_EXECUTION_METHODS(X, I)                                                          \
  X(I, R1, int, admit, const praktor_execution_admission *, admission)                            \
  X(I, R2, int, start, const praktor_execution_request *, request, praktor_service_job *, out)    \
  X(I, R2, int, poll, praktor_service_job *, job, praktor_execution_result *, out)                 \
  X(I, R1, int, cancel, praktor_service_job *, job)                                               \
  X(I, R1, int, release, praktor_service_job *, job)
CMETA_INTERFACE(praktor_execution_service, PRAKTOR_EXECUTION_METHODS);

#endif
