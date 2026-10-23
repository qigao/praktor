#include "praktor/service_process_host.h"
#include "praktor/process_worker.h"
#include <stdlib.h>
#include <string.h>

typedef struct process_host {
  praktor_service_config config;
  praktor_process_worker worker;
  const char **args;
  bool busy, closing;
} process_host;
static int run(void *user, const praktor_service_command *command, praktor_service_response *out) {
  process_host *host = user;
  praktor_process_worker_config config;
  praktor_process_worker_view view;
  int r;
  if (host->busy || host->closing) return SALTS_EBUSY;
  if (!command || !out || command->arguments.count > host->config.max_arguments ||
      (command->arguments.count && !command->arguments.values) ||
      command->max_output_bytes > host->config.max_output_bytes) return SALTS_EINVAL;
  host->busy = true;
  r = praktor_process_worker_destroy(&host->worker); if (r != SALTS_OK) goto done;
  if (command->arguments.count) memcpy(host->args,command->arguments.values,command->arguments.count*sizeof(char *));
  host->args[command->arguments.count] = NULL;
  praktor_process_worker_config_init(&config);
  config.options.program = command->program; config.options.args = host->args;
  config.options.flags = 0; config.options.timeout_ms = command->timeout_ms;
  config.max_arguments = host->config.max_arguments;
  config.max_metadata_bytes = host->config.max_metadata_bytes;
  config.max_output_bytes = command->max_output_bytes;
  r = praktor_process_worker_start(&host->worker,&config); if (r != SALTS_OK) goto done;
  r = praktor_process_worker_wait(&host->worker,UINT64_MAX); if (r != SALTS_OK) goto done;
  r = praktor_process_worker_result(&host->worker,&view); if (r != SALTS_OK) goto done;
  memset(out,0,sizeof(*out)); out->state = view.state; out->exit_code = view.result.exit_code;
  out->output = view.stdout_data; out->output_size = view.stdout_size;
 done:
  host->busy = false; return r;
}
int praktor_service_process_host_init(praktor_service_process_host *out, const praktor_service_config *config) {
  process_host *host;
  if (!out || out->impl || !config || !config->max_arguments ||
      config->max_arguments >= SIZE_MAX/sizeof(char *) || !config->max_metadata_bytes || !config->max_output_bytes) return SALTS_EINVAL;
  host = calloc(1,sizeof(*host)); if (!host) return SALTS_ENOMEM;
  host->config = *config; host->args = calloc(config->max_arguments+1u,sizeof(char *));
  if (!host->args) { free(host); return SALTS_ENOMEM; }
  out->impl = host; return SALTS_OK;
}
int praktor_service_process_host_bind(praktor_service_process_host *host, praktor_service_host *out) {
  if (!host || !host->impl || !out || ((process_host *)host->impl)->closing) return SALTS_EINVAL;
  memset(out,0,sizeof(*out)); out->user = host->impl; out->run = run; return SALTS_OK;
}
int praktor_service_process_host_close(praktor_service_process_host *out) {
  process_host *host; int r;
  if (!out || !out->impl) return SALTS_OK;
  host = out->impl; if (host->busy) return SALTS_EBUSY;
  host->closing = true; r = praktor_process_worker_destroy(&host->worker);
  if (r != SALTS_OK) return r;
  free(host->args); free(host); out->impl = NULL; return SALTS_OK;
}
