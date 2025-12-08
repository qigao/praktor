#include "unity.h"

#include "test_uv_fixture.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static JSUVTestEnv env;

void setUp(void) {
    js_uv_test_env_init(&env);
}

void tearDown(void) {
    js_uv_test_env_cleanup(&env);
}

static char *dup_string(JSValue value) {
    TEST_ASSERT_TRUE(JS_IsString(value));
    size_t len = 0;
    const char *str = JS_ToCStringLen(env.ctx, &len, value);
    TEST_ASSERT_NOT_NULL(str);
    char *copy = (char *)malloc(len + 1);
    TEST_ASSERT_NOT_NULL(copy);
    memcpy(copy, str, len);
    copy[len] = '\0';
    JS_FreeCString(env.ctx, str);
    return copy;
}

static JSValue get_global(const char *name) {
    JSValue prop = js_uv_test_global_prop(&env, name);
    TEST_ASSERT_FALSE(JS_IsException(prop));
    return prop;
}

void test_os_queries_return_values(void) {
    js_uv_test_eval(&env,
                    "const os = uv.os;\n"
                    "globalThis.osHostname = os.hostname();\n"
                    "globalThis.osHomedir = os.homedir();\n"
                    "globalThis.osTmpdir = os.tmpdir();\n"
                    "globalThis.osUptime = os.uptime();\n"
                    "globalThis.osLoad = os.loadavg();\n");

    JSValue hostname = get_global("osHostname");
    char *hostname_str = dup_string(hostname);
    TEST_ASSERT_NOT_EQUAL(0, (int)strlen(hostname_str));
    free(hostname_str);
    JS_FreeValue(env.ctx, hostname);

    JSValue homedir = get_global("osHomedir");
    char *homedir_str = dup_string(homedir);
    TEST_ASSERT_NOT_EQUAL(0, (int)strlen(homedir_str));
    free(homedir_str);
    JS_FreeValue(env.ctx, homedir);

    JSValue tmpdir = get_global("osTmpdir");
    char *tmpdir_str = dup_string(tmpdir);
    TEST_ASSERT_NOT_EQUAL(0, (int)strlen(tmpdir_str));
    free(tmpdir_str);
    JS_FreeValue(env.ctx, tmpdir);

    JSValue uptime = get_global("osUptime");
    double uptime_val = 0.0;
    TEST_ASSERT_EQUAL_INT(0, JS_ToFloat64(env.ctx, &uptime_val, uptime));
    TEST_ASSERT_TRUE(uptime_val >= 0.0);
    JS_FreeValue(env.ctx, uptime);

    JSValue load = get_global("osLoad");
    TEST_ASSERT_TRUE(JS_IsArray(load));
    int64_t length = 0;
    TEST_ASSERT_EQUAL_INT(0, JS_GetLength(env.ctx, load, &length));
    TEST_ASSERT_EQUAL_INT(3, (int)length);
    JS_FreeValue(env.ctx, load);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_os_queries_return_values);
    return UNITY_END();
}
