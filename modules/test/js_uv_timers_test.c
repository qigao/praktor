#include "unity.h"

#include "test_uv_fixture.h"

#include <stdlib.h>
#include <string.h>

static JSUVTestEnv env;

void setUp(void) {
    js_uv_test_env_init(&env);
}

void tearDown(void) {
    js_uv_test_env_cleanup(&env);
}

static int32_t get_int_global(const char *name) {
    JSValue prop = js_uv_test_global_prop(&env, name);
    int32_t value = 0;
    TEST_ASSERT_EQUAL_INT(0, JS_ToInt32(env.ctx, &value, prop));
    JS_FreeValue(env.ctx, prop);
    return value;
}

static const char *get_string_global(const char *name) {
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

void test_set_timeout_executes_callback(void) {
    js_uv_test_eval(&env,
                    "globalThis.timerFired = 0;\n"
                    "uv.setTimeout(() => { globalThis.timerFired = 1; }, 5);\n");
    js_uv_test_run_loop(&env);
    TEST_ASSERT_EQUAL_INT(1, get_int_global("timerFired"));
}

void test_clear_timer_prevents_callback(void) {
    js_uv_test_eval(&env,
                    "globalThis.cleared = 0;\n"
                    "const id = uv.setTimeout(() => { globalThis.cleared = 1; }, 5);\n"
                    "uv.clearTimer(id);\n");
    js_uv_test_run_loop(&env);
    TEST_ASSERT_EQUAL_INT(0, get_int_global("cleared"));
}

void test_uv_sleep_resolves_promise(void) {
    js_uv_test_eval(&env,
                    "globalThis.sleepResult = 'pending';\n"
                    "uv.sleep(5)\n"
                    "  .then(() => { globalThis.sleepResult = 'done'; })\n"
                    "  .catch(() => { globalThis.sleepResult = 'error'; });\n");
    js_uv_test_run_loop(&env);
    const char *result = get_string_global("sleepResult");
    TEST_ASSERT_EQUAL_STRING("done", result);
    free((void *)result);
}

void test_set_interval_runs_multiple_times(void) {
    js_uv_test_eval(&env,
                    "globalThis.intervalTicks = 0;\n"
                    "const id = uv.setInterval(() => {\n"
                    "  globalThis.intervalTicks += 1;\n"
                    "  if (globalThis.intervalTicks >= 3) {\n"
                    "    uv.clearTimer(id);\n"
                    "  }\n"
                    "}, 5);\n");
    js_uv_test_run_loop(&env);
    TEST_ASSERT_EQUAL_INT(3, get_int_global("intervalTicks"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_set_timeout_executes_callback);
    RUN_TEST(test_clear_timer_prevents_callback);
    RUN_TEST(test_uv_sleep_resolves_promise);
    RUN_TEST(test_set_interval_runs_multiple_times);
    return UNITY_END();
}