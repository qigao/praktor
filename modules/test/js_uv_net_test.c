#include "unity.h"

#include "test_uv_fixture.h"

#include <stdint.h>

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

static int get_bool_global(const char *name) {
    JSValue prop = js_uv_test_global_prop(&env, name);
    int result = JS_ToBool(env.ctx, prop);
    JS_FreeValue(env.ctx, prop);
    return result;
}

void test_net_ip_helpers(void) {
    js_uv_test_eval(&env,
                    "const net = uv.net;\n"
                    "globalThis.isIPVal = net.isIP('127.0.0.1');\n"
                    "globalThis.isIPv4Val = net.isIPv4('127.0.0.1');\n"
                    "globalThis.isIPv6Val = net.isIPv6('::1');\n"
                    "globalThis.isIPv4False = net.isIPv4('::1');\n");

    TEST_ASSERT_EQUAL_INT(4, get_int_global("isIPVal"));
    TEST_ASSERT_NOT_EQUAL(0, get_bool_global("isIPv4Val"));
    TEST_ASSERT_NOT_EQUAL(0, get_bool_global("isIPv6Val"));
    TEST_ASSERT_EQUAL_INT(0, get_bool_global("isIPv4False"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_net_ip_helpers);
    return UNITY_END();
}