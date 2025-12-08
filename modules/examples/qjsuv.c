#include "js_uv_module.h"
#include "quickjs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static JSValue js_print_to_console(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    for (int i = 0; i < argc; ++i) {
        const char *str = JS_ToCString(ctx, argv[i]);
        if (str) {
            fputs(str, stdout);
            JS_FreeCString(ctx, str);
        }
        if (i < argc - 1) {
            fputc(' ', stdout);
        }
    }
    fputc('\n', stdout);
    return JS_UNDEFINED;
}

static void init_console(JSContext *ctx) {
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue console_obj = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, console_obj, "log", JS_NewCFunction(ctx, js_print_to_console, "log", 1));
    JS_SetPropertyStr(ctx, global_obj, "console", console_obj);

    JS_FreeValue(ctx, global_obj);
}

static void dump_exception(JSContext *ctx, JSValueConst exception) {
    const char *str = JS_ToCString(ctx, exception);
    if (str) {
        fprintf(stderr, "%s\n", str);
        JS_FreeCString(ctx, str);
    } else {
        fprintf(stderr, "[exception]\n");
    }
    if (JS_IsObject(exception)) {
        JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
        if (!JS_IsUndefined(stack)) {
            const char *stack_str = JS_ToCString(ctx, stack);
            if (stack_str) {
                fprintf(stderr, "%s\n", stack_str);
                JS_FreeCString(ctx, stack_str);
            }
        }
        JS_FreeValue(ctx, stack);
    }
}

static void evaluate_script(JSContext *ctx, const char *script) {
    JSValue result = JS_Eval(ctx, script, strlen(script), "<uv-demo>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        JSValue exception = JS_GetException(ctx);
        dump_exception(ctx, exception);
        JS_FreeValue(ctx, exception);
    }
    JS_FreeValue(ctx, result);
}

int main(void) {
    uv_loop_t loop;
    if (uv_loop_init(&loop) != 0) {
        fprintf(stderr, "Failed to initialise libuv loop\n");
        return EXIT_FAILURE;
    }

    JSRuntime *runtime = JS_NewRuntime();
    if (!runtime) {
        fprintf(stderr, "Unable to allocate QuickJS runtime\n");
        uv_loop_close(&loop);
        return EXIT_FAILURE;
    }

    JSContext *ctx = JS_NewContextRaw(runtime);
    if (!ctx) {
        fprintf(stderr, "Unable to allocate QuickJS context\n");
        JS_FreeRuntime(runtime);
        uv_loop_close(&loop);
        return EXIT_FAILURE;
    }

    JS_AddIntrinsicBaseObjects(ctx);
    JS_AddIntrinsicDate(ctx);
    JS_AddIntrinsicEval(ctx);
    JS_AddIntrinsicRegExp(ctx);
    JS_AddIntrinsicJSON(ctx);
    JS_AddIntrinsicProxy(ctx);
    JS_AddIntrinsicMapSet(ctx);
    JS_AddIntrinsicTypedArrays(ctx);
    JS_AddIntrinsicPromise(ctx);
    JS_AddIntrinsicBigInt(ctx);

    init_console(ctx);

    if (js_init_uv_module(ctx, &loop) != 0) {
        fprintf(stderr, "Failed to initialise uv module\n");
        JS_FreeContext(ctx);
        JS_FreeRuntime(runtime);
        uv_loop_close(&loop);
        return EXIT_FAILURE;
    }

    const char *script =
        "const uv = globalThis.uv;\n"
        "async function main() {\n"
        "  console.log('uv demo start');\n"
        "  await uv.sleep(100);\n"
        "  let tick = 0;\n"
        "  const intervalId = uv.setInterval(() => {\n"
        "    tick += 1;\n"
        "    console.log('tick', tick);\n"
        "    if (tick === 3) {\n"
        "      uv.clearTimer(intervalId);\n"
        "      console.log('interval cleared');\n"
        "    }\n"
        "  }, 75);\n"
        "  await uv.sleep(350);\n"
        "  await new Promise((resolve) => {\n"
        "    uv.setTimeout(() => {\n"
        "      console.log('timeout finished');\n"
        "      resolve();\n"
        "    }, 60);\n"
        "  });\n"
        "  console.log('uv demo done');\n"
        "}\n"
        "main().catch((err) => {\n"
        "  console.log('uv demo error', err && err.stack ? err.stack : err);\n"
        "});\n";

    evaluate_script(ctx, script);
    js_uv_run_loop(runtime);

    int loop_close_rc = uv_loop_close(&loop);
    if (loop_close_rc != 0) {
        fprintf(stderr, "uv_loop_close: %s\n", uv_strerror(loop_close_rc));
    }

    JS_FreeContext(ctx);
    JS_FreeRuntime(runtime);
    return 0;
}
