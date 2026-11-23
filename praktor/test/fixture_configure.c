#include <core/tstr.h>
#include <file/fs.h>
#include <stdio.h>
#include <string.h>

/* Configure the same test templates that CMake uses. */
int main(int argc, char **argv) {
  if (argc < 3 || (argc - 3) % 2 != 0) {
    fputs("Usage: fixture_configure input output [token replacement ...]\n", stderr);
    return 2;
  }
  fs_buf_t input = {0};
  if (fs_read_file(argv[1], &input) != 0) {
    fprintf(stderr, "Cannot read fixture template %s\n", argv[1]);
    return 1;
  }
  tstr text = tstr_from_v((vstr){input.base, input.len});
  fs_buf_free(&input);
  if (!text) return 1;
  for (int i = 3; i < argc; i += 2) {
    const char *token = argv[i];
    if (!*token || !strstr(text, token)) {
      fprintf(stderr, "Missing fixture token %s in %s\n", token, argv[1]);
      tstr_free(text);
      return 1;
    }
    text = tstr_replace_all(text, token, argv[i + 1]);
    if (!text || strstr(text, token)) {
      fprintf(stderr, "Cannot substitute fixture token %s\n", token);
      tstr_free(text);
      return 1;
    }
  }
  fs_buf_t output = {.base = text, .len = tstr_len(text)};
  const int status = fs_write_file(argv[2], &output);
  tstr_free(text);
  if (status != 0) fprintf(stderr, "Cannot write fixture %s\n", argv[2]);
  return status == 0 ? 0 : 1;
}
