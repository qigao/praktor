#include <core/clock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads/threads.h>
/* Formal test provider protocol: argv input and stdout/stderr output. */
int main(int argc, char **argv) {
  if (argc < 2)
    return 2;
  if (!strcmp(argv[1], "emit")) {
    if (argc != 3)
      return 2;
    return fwrite(argv[2], 1u, strlen(argv[2]), stdout) == strlen(argv[2]) ? 0 : 3;
  }
  if (!strcmp(argv[1], "sleep")) {
    sleep_ms(10000u);
    return 0;
  }
  if (!strcmp(argv[1], "env")) {
    if (argc != 3)
      return 2;
    const char *value = getenv(argv[2]);
    if (!value)
      return 4;
    return fwrite(value, 1u, strlen(value), stdout) == strlen(value) ? 0 : 3;
  }
  if (!strcmp(argv[1], "fail")) {
    fputs("fixture refused the operation", stderr);
    return 7;
  }
  if (!strcmp(argv[1], "binary")) {
    const char bytes[] = {'a', '\0', 'b'};
    fwrite(bytes, 1u, sizeof bytes, stdout);
    return 0;
  }
  if (!strcmp(argv[1], "bulk")) {
    for (size_t i = 0u; i < 8192u; ++i)
      fputc('x', stdout);
    return 0;
  }
  return 2;
}
