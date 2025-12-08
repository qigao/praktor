#include "unity.h"

#include "test_uv_fixture.h"

static JSUVTestEnv env;

void setUp(void) {
    js_uv_test_env_init(&env);
}

void tearDown(void) {
    js_uv_test_env_cleanup(&env);
}

static void assert_function(JSValue value) {
    TEST_ASSERT_FALSE(JS_IsException(value));
    TEST_ASSERT_TRUE(JS_IsFunction(env.ctx, value));
}

void test_uv_global_object_present(void) {
    JSValue uv_obj = js_uv_test_global_prop(&env, "uv");
    TEST_ASSERT_TRUE(JS_IsObject(uv_obj));

    JSValue timers = JS_GetPropertyStr(env.ctx, uv_obj, "setTimeout");
    assert_function(timers);
    JS_FreeValue(env.ctx, timers);

    JSValue fs = JS_GetPropertyStr(env.ctx, uv_obj, "fs");
    TEST_ASSERT_TRUE(JS_IsObject(fs));
    JS_FreeValue(env.ctx, fs);

    JSValue dns = JS_GetPropertyStr(env.ctx, uv_obj, "dns");
    TEST_ASSERT_TRUE(JS_IsObject(dns));
    JS_FreeValue(env.ctx, dns);

    JSValue proc = JS_GetPropertyStr(env.ctx, uv_obj, "proc");
    TEST_ASSERT_TRUE(JS_IsObject(proc));
    JS_FreeValue(env.ctx, proc);

    JSValue net = JS_GetPropertyStr(env.ctx, uv_obj, "net");
    TEST_ASSERT_TRUE(JS_IsObject(net));
    JS_FreeValue(env.ctx, net);

    JSValue os = JS_GetPropertyStr(env.ctx, uv_obj, "os");
    TEST_ASSERT_TRUE(JS_IsObject(os));
    JS_FreeValue(env.ctx, os);

    JSValue signal = JS_GetPropertyStr(env.ctx, uv_obj, "signal");
    TEST_ASSERT_TRUE(JS_IsObject(signal));
    JS_FreeValue(env.ctx, signal);

    JS_FreeValue(env.ctx, uv_obj);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_uv_global_object_present);
    return UNITY_END();
}