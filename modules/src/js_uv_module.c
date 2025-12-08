#include "js_uv_internal.h"

#include <stdlib.h>

static void js_uv_flush_jobs(JSRuntime *rt) {
    JSContext *ctx = NULL;
    while (JS_IsJobPending(rt)) {
        int err = JS_ExecutePendingJob(rt, &ctx);
        if (err < 0 && ctx) {
            js_uv_dump_error(ctx);
        }
    }
}

int js_init_uv_module(JSContext *ctx, uv_loop_t *loop) {
    if (!ctx || !loop) {
        return -1;
    }

    js_uv_set_loop(loop);

    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue uv_obj = JS_NewObject(ctx);
    if (JS_IsException(uv_obj)) {
        JS_FreeValue(ctx, global_obj);
        return -1;
    }

    int rc = 0;
    if (js_uv_register_timers(ctx, uv_obj) < 0 ||
        js_uv_register_fs(ctx, uv_obj) < 0 ||
        js_uv_register_dns(ctx, uv_obj) < 0 ||
        js_uv_register_process(ctx, uv_obj) < 0 ||
        js_uv_register_net(ctx, uv_obj) < 0 ||
        js_uv_register_os(ctx, uv_obj) < 0 ||
        js_uv_register_signal(ctx, uv_obj) < 0) {
        rc = -1;
    }

    if (rc == 0) {
        if (JS_SetPropertyStr(ctx, global_obj, "uv", uv_obj) < 0) {
            rc = -1;
        }
    }

    if (rc != 0) {
        JS_FreeValue(ctx, uv_obj);
    }
    JS_FreeValue(ctx, global_obj);
    return rc;
}

void js_uv_run_loop(JSRuntime *rt) {
    uv_loop_t *loop = js_uv_get_loop();
    if (!loop) {
        return;
    }

    for (;;) {
        js_uv_flush_jobs(rt);
        if (!uv_loop_alive(loop)) {
            if (!JS_IsJobPending(rt)) {
                break;
            }
        }
        uv_run(loop, JS_IsJobPending(rt) ? UV_RUN_NOWAIT : UV_RUN_ONCE);
        if (!uv_loop_alive(loop) && !JS_IsJobPending(rt)) {
            break;
        }
    }

    uv_run(loop, UV_RUN_NOWAIT);
    js_uv_flush_jobs(rt);
}
