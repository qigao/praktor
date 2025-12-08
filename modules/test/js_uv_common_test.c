#include "test_uv_fixture.h"
#include <stdint.h>
#include "unity.h"
#include <stdlib.h>
#include <string.h>

static JSUVTestEnv env;
void setUp(void) { js_uv_test_env_init(&env); }
void tearDown(void) { js_uv_test_env_cleanup(&env); }
void test_buffer_append_grows_buffer(void) {
  JSUVByteBuffer buf;
  js_uv_buffer_init(&buf);
  const uint8_t sample[] = {1, 2, 3, 4};
  TEST_ASSERT_EQUAL_INT(0, js_uv_buffer_append(&buf, sample, sizeof(sample)));
  TEST_ASSERT_EQUAL_UINT(sizeof(sample), buf.length);
  TEST_ASSERT_NOT_NULL(buf.data);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(sample, buf.data, sizeof(sample));
  js_uv_buffer_free(&buf);
  TEST_ASSERT_NULL(buf.data);
  TEST_ASSERT_EQUAL_UINT(0, buf.length);
  TEST_ASSERT_EQUAL_UINT(0, buf.capacity);
}
void test_collect_data_from_string(void) {
  JSValue str_val = JS_NewString(env.ctx, "hello");
  TEST_ASSERT_FALSE(JS_IsException(str_val));
  uint8_t *data = NULL;
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(0, js_uv_collect_data(env.ctx, str_val, &data, &len));
  TEST_ASSERT_NOT_NULL(data);
  TEST_ASSERT_EQUAL_UINT(5, len);
  TEST_ASSERT_EQUAL_UINT8('h', data[0]);
  TEST_ASSERT_EQUAL_UINT8('o', data[4]);
  free(data);
  JS_FreeValue(env.ctx, str_val);
}
void test_collect_data_from_array_buffer(void) {
  const uint8_t sample[] = {42, 24, 7};
  JSValue array_buffer = JS_NewArrayBufferCopy(env.ctx, sample, sizeof(sample));
  TEST_ASSERT_FALSE(JS_IsException(array_buffer));
  uint8_t *data = NULL;
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(0, js_uv_collect_data(env.ctx, array_buffer, &data, &len));
  TEST_ASSERT_NOT_NULL(data);
  TEST_ASSERT_EQUAL_UINT(sizeof(sample), len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(sample, data, sizeof(sample));
  free(data);
  JS_FreeValue(env.ctx, array_buffer);
}
void test_collect_data_from_typed_array(void) {
  JSValue typed = js_uv_test_eval_value(&env, "(function(){ return new Uint8Array([9,8,7]); })();");
  const uint8_t expected[] = {9, 8, 7};
  uint8_t *data = NULL;
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(0, js_uv_collect_data(env.ctx, typed, &data, &len));
  TEST_ASSERT_NOT_NULL(data);
  TEST_ASSERT_EQUAL_UINT(sizeof(expected), len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, data, sizeof(expected));
  free(data);
  JS_FreeValue(env.ctx, typed);
}
void test_make_uv_error_sets_properties(void) {
  JSValue error = js_uv_make_uv_error(env.ctx, UV_EINVAL, "uv_test_syscall");
  TEST_ASSERT_FALSE(JS_IsException(error));
  TEST_ASSERT_TRUE(JS_IsObject(error));
  JSValue msg_val = JS_GetPropertyStr(env.ctx, error, "message");
  TEST_ASSERT_FALSE(JS_IsException(msg_val));
  const char *msg = JS_ToCString(env.ctx, msg_val);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_GREATER_THAN_UINT(0, (uint32_t)strlen(msg));
  JSValue code_val = JS_GetPropertyStr(env.ctx, error, "code");
  TEST_ASSERT_FALSE(JS_IsException(code_val));
  const char *code = JS_ToCString(env.ctx, code_val);
  TEST_ASSERT_NOT_NULL(code);
  TEST_ASSERT_GREATER_THAN_UINT(0, (uint32_t)strlen(code));
  JSValue syscall_val = JS_GetPropertyStr(env.ctx, error, "syscall");
  TEST_ASSERT_FALSE(JS_IsException(syscall_val));
  const char *syscall_str = JS_ToCString(env.ctx, syscall_val);
  TEST_ASSERT_NOT_NULL(syscall_str);
  TEST_ASSERT_EQUAL_STRING("uv_test_syscall", syscall_str);
  JS_FreeCString(env.ctx, syscall_str);
  JS_FreeValue(env.ctx, syscall_val);
  JS_FreeCString(env.ctx, code);
  JS_FreeValue(env.ctx, code_val);
  JS_FreeCString(env.ctx, msg);
  JS_FreeValue(env.ctx, msg_val);
  JS_FreeValue(env.ctx, error);
}
int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_buffer_append_grows_buffer);
  RUN_TEST(test_collect_data_from_string);
  RUN_TEST(test_collect_data_from_array_buffer);
  RUN_TEST(test_collect_data_from_typed_array);
  RUN_TEST(test_make_uv_error_sets_properties);
  return UNITY_END();
}
