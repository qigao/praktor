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

void test_dns_getaddrinfo_localhost(void) {
    js_uv_test_eval(&env,
                    "globalThis.dnsLength = -999;\n"
                    "uv.dns.getAddrInfo('localhost')\n"
                    "  .then((res) => { globalThis.dnsLength = res.length; })\n"
                    "  .catch(() => { globalThis.dnsLength = -1; });\n");
    js_uv_test_run_loop(&env);
    int32_t len = get_int_global("dnsLength");
    TEST_ASSERT_GREATER_THAN_INT(0, len);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dns_getaddrinfo_localhost);
    return UNITY_END();
}
