#pragma once

#include "quickjs.h"
#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Expose libuv-backed utilities to a QuickJS context. */
int js_init_uv_module(JSContext *ctx, uv_loop_t *loop);

/* Run the libuv loop while flushing the QuickJS job queue. */
void js_uv_run_loop(JSRuntime *rt);

#ifdef __cplusplus
} /* extern "C" */
#endif
