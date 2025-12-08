#include "unity.h"

#include "test_uv_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <direct.h>
#else
  #include <sys/stat.h>
  #include <unistd.h>
#endif

static JSUVTestEnv env;

void setUp(void) { js_uv_test_env_init(&env); }

void tearDown(void) { js_uv_test_env_cleanup(&env); }

static void make_temp_path(char *out, size_t out_len) {
  char tmpdir[512];
  size_t len = sizeof(tmpdir);
  TEST_ASSERT_EQUAL_INT(0, uv_os_tmpdir(tmpdir, &len));
  int need_sep = (len == 0 || (tmpdir[len - 1] != '/' && tmpdir[len - 1] != '\\'));
  unsigned long long unique = (unsigned long long)uv_hrtime();
  if (need_sep) {
    snprintf(out, out_len, "%s\\uvtest_%llu.txt", tmpdir, unique);
  } else {
    snprintf(out, out_len, "%suvtest_%llu.txt", tmpdir, unique);
  }
}

static void make_temp_dir(char *out, size_t out_len) {
  char tmpdir[512];
  size_t len = sizeof(tmpdir);
  TEST_ASSERT_EQUAL_INT(0, uv_os_tmpdir(tmpdir, &len));
  int need_sep = (len == 0 || (tmpdir[len - 1] != '/' && tmpdir[len - 1] != '\\'));
#ifdef _WIN32
  const char sep = '\\';
#else
  const char sep = '/';
#endif
  unsigned long long unique = (unsigned long long)uv_hrtime();
  int written;
  if (need_sep) {
    written = snprintf(out, out_len, "%s%cuvtest_dir_%llu", tmpdir, sep, unique);
  } else {
    written = snprintf(out, out_len, "%suvtest_dir_%llu", tmpdir, unique);
  }
  TEST_ASSERT_TRUE(written > 0);
  TEST_ASSERT_TRUE((size_t)written < out_len);
#ifdef _WIN32
  TEST_ASSERT_EQUAL_INT(0, _mkdir(out));
#else
  TEST_ASSERT_EQUAL_INT(0, mkdir(out, 0700));
#endif
}

static void escape_js_string(const char *input, char *output, size_t out_len) {
  size_t j = 0;
  for (size_t i = 0; input[i] != '\0' && j + 2 < out_len; ++i) {
    char c = input[i];
    if (c == '\\' || c == '"') {
      output[j++] = '\\';
    }
    output[j++] = c;
  }
  output[j] = '\0';
}

static const char *dup_string_global(const char *name) {
  JSValue prop = js_uv_test_global_prop(&env, name);
  TEST_ASSERT_TRUE(JS_IsString(prop));
  size_t len = 0;
  const char *str = JS_ToCStringLen(env.ctx, &len, prop);
  TEST_ASSERT_NOT_NULL(str);
  char *copy = (char *)malloc(len + 1);
  TEST_ASSERT_NOT_NULL(copy);
  memcpy(copy, str, len);
  copy[len] = '\0';
  JS_FreeCString(env.ctx, str);
  JS_FreeValue(env.ctx, prop);
  return copy;
}

static JSValue get_global(const char *name) {
  JSValue prop = js_uv_test_global_prop(&env, name);
  TEST_ASSERT_FALSE(JS_IsException(prop));
  return prop;
}

void test_fs_write_and_read_file(void) {
  char path[512];
  make_temp_path(path, sizeof(path));
  char literal[1024];
  escape_js_string(path, literal, sizeof(literal));

  char script[2048];
  snprintf(script, sizeof(script),
           "const fs = uv.fs;\n"
           "globalThis.fsResult = null;\n"
           "fs.writeFile(\"%s\", \"unity_file_data\")\n"
           "  .then(() => fs.readFile(\"%s\"))\n"
           "  .then((data) => { globalThis.fsResult = data; })\n"
           "  .catch((err) => { globalThis.fsResult = 'ERROR:' + err.message; });\n",
           literal, literal);
  js_uv_test_eval(&env, script);
  js_uv_test_run_loop(&env);

  const char *result = dup_string_global("fsResult");
  TEST_ASSERT_EQUAL_STRING("unity_file_data", result);
  free((void *)result);

  remove(path);
}

void test_fs_stat_reports_file(void) {
  char path[512];
  make_temp_path(path, sizeof(path));
  FILE *fp = fopen(path, "wb");
  TEST_ASSERT_NOT_NULL(fp);
  fputs("stat-data", fp);
  fclose(fp);

  char literal[1024];
  escape_js_string(path, literal, sizeof(literal));

  char script[1024];
  snprintf(script, sizeof(script),
           "const fs = uv.fs;\n"
           "globalThis.statResult = null;\n"
           "fs.stat(\"%s\")\n"
           "  .then((info) => { globalThis.statResult = info.isFile; })\n"
           "  .catch(() => { globalThis.statResult = false; });\n",
           literal);
  js_uv_test_eval(&env, script);
  js_uv_test_run_loop(&env);

  JSValue result = get_global("statResult");
  TEST_ASSERT_TRUE(JS_IsBool(result));
  TEST_ASSERT_TRUE(JS_ToBool(env.ctx, result));
  JS_FreeValue(env.ctx, result);

  remove(path);
}

void test_fs_readdir_lists_file(void) {
  char dir[512];
  make_temp_dir(dir, sizeof(dir));
#ifdef _WIN32
  const char sep = '\\';
#else
  const char sep = '/';
#endif
  const char *filename = "uv_entry.txt";
  char file_path[512];
  snprintf(file_path, sizeof(file_path), "%s%c%s", dir, sep, filename);
  FILE *fp = fopen(file_path, "wb");
  TEST_ASSERT_NOT_NULL(fp);
  fputs("dir-data", fp);
  fclose(fp);

  char literal[1024];
  escape_js_string(dir, literal, sizeof(literal));

  char script[2048];
  snprintf(script, sizeof(script),
           "const fs = uv.fs;\n"
           "globalThis.dirResult = null;\n"
           "fs.readdir(\"%s\")\n"
           "  .then((entries) => { globalThis.dirResult = entries.map((e) => "
           "e.name).sort().join(\",\"); })\n"
           "  .catch((err) => { globalThis.dirResult = 'ERROR:' + err.message; });\n",
           literal);
  js_uv_test_eval(&env, script);
  js_uv_test_run_loop(&env);

  const char *result = dup_string_global("dirResult");
  TEST_ASSERT_EQUAL_STRING(filename, result);
  free((void *)result);

  TEST_ASSERT_EQUAL_INT(0, remove(file_path));
#ifdef _WIN32
  TEST_ASSERT_EQUAL_INT(0, _rmdir(dir));
#else
  TEST_ASSERT_EQUAL_INT(0, rmdir(dir));
#endif
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_fs_write_and_read_file);
  RUN_TEST(test_fs_stat_reports_file);
  RUN_TEST(test_fs_readdir_lists_file);
  return UNITY_END();
}
