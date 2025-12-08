#ifndef JS_UV_TEST_FIXTURE_H
#define JS_UV_TEST_FIXTURE_H
#include <string.h>
#include <uv.h>

#include "js_uv_internal.h"
#include "quickjs.h"
#include "unity.h"

typedef struct
{
  JSRuntime* rt;
  JSContext* ctx;
  uv_loop_t loop;
} JSUVTestEnv;

static inline void js_uv_test_env_init(JSUVTestEnv* env)
{
  memset(env, 0, sizeof(*env));
  TEST_ASSERT_EQUAL_INT(0, uv_loop_init(&env->loop));
  env->rt = JS_NewRuntime();
  TEST_ASSERT_NOT_NULL(env->rt);
  JS_SetMemoryLimit(env->rt, -1);
  JS_SetMaxStackSize(env->rt, 0);
  env->ctx = JS_NewContext(env->rt);
  TEST_ASSERT_NOT_NULL(env->ctx);
  TEST_ASSERT_EQUAL_INT(0, js_init_uv_module(env->ctx, &env->loop));
}

static inline void js_uv_test_env_cleanup(JSUVTestEnv* env)
{
  if (env->ctx) {
    JS_FreeContext(env->ctx);
    env->ctx = NULL;
  }
  if (env->rt) {
    JS_FreeRuntime(env->rt);
    env->rt = NULL;
  }
  TEST_ASSERT_EQUAL_INT(0, uv_loop_close(&env->loop));
  memset(&env->loop, 0, sizeof(env->loop));
}

static inline void js_uv_test_run_loop(JSUVTestEnv* env)
{
  js_uv_run_loop(env->rt);
}

static inline void js_uv_test_eval(JSUVTestEnv* env, const char* code)
{
  JSValue result =
      JS_Eval(env->ctx, code, strlen(code), "<test>", JS_EVAL_TYPE_GLOBAL);
  TEST_ASSERT_FALSE(JS_IsException(result));
  JS_FreeValue(env->ctx, result);
}

static inline JSValue js_uv_test_eval_value(JSUVTestEnv* env, const char* code)
{
  JSValue result =
      JS_Eval(env->ctx, code, strlen(code), "<test>", JS_EVAL_TYPE_GLOBAL);
  TEST_ASSERT_FALSE(JS_IsException(result));
  return result;
}

static inline JSValue js_uv_test_global_prop(JSUVTestEnv* env, const char* name)
{
  JSValue global_obj = JS_GetGlobalObject(env->ctx);
  JSValue prop = JS_GetPropertyStr(env->ctx, global_obj, name);
  JS_FreeValue(env->ctx, global_obj);
  return prop;
}
#endif /* JS_UV_TEST_FIXTURE_H */
