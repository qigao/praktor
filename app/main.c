#include "praktor.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int timeout_value(const char *text, int *out) {
  char *end;
  long value;
  if (!text || *text < '0' || *text > '9')
    return 0;
  errno = 0;
  value = strtol(text, &end, 10);
  if (errno || *end || value < 1 || value > INT_MAX)
    return 0;
  *out = (int)value;
  return 1;
}

int main(int argc, char **argv) {
  int timeout_ms = 0;
  const char *config = NULL, *task = NULL;
  int named_config = 0;
  int check = 0;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--config")) {
      if (config || i + 1 >= argc || !argv[i + 1][0] || !strncmp(argv[i + 1], "--", 2u))
        goto usage;
      config = argv[++i];
      named_config = 1;
    } else if (!strcmp(argv[i], "--task")) {
      if (task || i + 1 >= argc || !argv[i + 1][0] || !strncmp(argv[i + 1], "--", 2u))
        goto usage;
      task = argv[++i];
    } else if (!strcmp(argv[i], "--timeout-ms")) {
      if (timeout_ms || i + 1 >= argc || !timeout_value(argv[++i], &timeout_ms))
        goto usage;
    } else if (!strcmp(argv[i], "--check")) {
      if (check)
        goto usage;
      check = 1;
    } else if (i == 1 && argv[i][0] != '-' && !config) {
      config = argv[i]; /* Preserve the existing positional file form. */
    } else
      goto usage;
  }
  if (!config || (check ? task || timeout_ms : named_config && !task))
    goto usage;
  FILE *file = fopen(config, "rb");
  if (!file) {
    perror(config);
    return 2;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    perror(config);
    fclose(file);
    return 2;
  }
  long len = ftell(file);
  if (len <= 0 || len > 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
    fprintf(stderr, "praktor: %s: expected a readable workflow of 1 byte to 1 MiB\n", config);
    fclose(file);
    return 2;
  }
  char *yaml = (char *)malloc((size_t)len + 1u);
  if (!yaml) {
    fputs("praktor: cannot allocate workflow buffer\n", stderr);
    fclose(file);
    return 2;
  }
  if (fread(yaml, 1u, (size_t)len, file) != (size_t)len) {
    fprintf(stderr, "praktor: %s: could not read the complete workflow\n", config);
    free(yaml);
    fclose(file);
    return 2;
  }
  fclose(file);
  yaml[len] = '\0';
  if (!check && praktor_load_environment(yaml, (size_t)len) != PRAKTOR_RUNTIME_OK) {
    fputs("praktor: workflow metadata or dotEnv loading failed\n", stderr);
    free(yaml);
    return 2;
  }
  praktor_execute_request request = {0};
  praktor_runtime_result result = {0};
  request.yaml = yaml;
  request.yaml_size = (size_t)len;
  request.limits = praktor_default_limits();
  if (timeout_ms)
    request.limits.timeout_ms = timeout_ms;
  praktor_runtime_status status;
  if (check)
    status = praktor_check(&request, &result);
  else
    status = task ? praktor_execute_task(&request, task, &result) : praktor_execute(&request, &result);
  if (result.json) {
    fwrite(result.json, 1u, result.json_size, stdout);
    fputc('\n', stdout);
  }
  if (status != PRAKTOR_RUNTIME_OK) {
    if (check)
      fprintf(stderr, "praktor: %s: %s\n", config,
              result.error[0] ? result.error : "workflow check failed");
    else if (result.error[0])
      fprintf(stderr, "praktor: %s\n", result.error);
  }
  praktor_release_result(&result);
  free(yaml);
  return status == PRAKTOR_RUNTIME_OK ? 0 : status >= 0 ? 1 : 2;
usage:
  fputs("usage: praktor --config workflow.yml --task NAME [--timeout-ms N]\n", stderr);
  fputs("       praktor --config workflow.yml --check\n", stderr);
  return 2;
}
