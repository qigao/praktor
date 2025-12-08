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

static char *dup_string_global(const char *name) {
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

static int get_bool_global(const char *name) {
    JSValue prop = js_uv_test_global_prop(&env, name);
    int result = JS_ToBool(env.ctx, prop);
    JS_FreeValue(env.ctx, prop);
    return result;
}

void test_signal_watch_and_stop_closes_handle(void) {
    js_uv_test_eval(&env,
                    "globalThis.signalStatus = 'pending';\n"
                    "globalThis.signalClosed = false;\n"
                    "const watcher = uv.signal.watch(2, () => { globalThis.signalStatus = 'fired'; });\n"
                    "watcher.onClose(() => { globalThis.signalClosed = true; });\n"
                    "watcher.stop();\n");
    js_uv_test_run_loop(&env);

    char *status = dup_string_global("signalStatus");
    TEST_ASSERT_EQUAL_STRING("pending", status);
    free(status);
    TEST_ASSERT_NOT_EQUAL(0, get_bool_global("signalClosed"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_signal_watch_and_stop_closes_handle);
    return UNITY_END();
}