#include "js_uv_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct JSTimer {
  uv_timer_t handle;
  JSContext *ctx;
  JSValue callback;
  uint32_t id;
  bool closing;
  struct JSTimer *prev;
  struct JSTimer *next;
} JSTimer;

typedef struct JSSleepRequest {
  uv_timer_t handle;
  JSContext *ctx;
  JSValue resolve;
  JSValue reject;
} JSSleepRequest;

static JSTimer *g_timer_head = NULL;
static uint32_t g_next_timer_id = 1;

enum { JS_UV_TIMER_ONCE = 0, JS_UV_TIMER_INTERVAL = 1 };

static void js_uv_add_timer(JSTimer *timer) {
  timer->prev = NULL;
  timer->next = g_timer_head;
  if (g_timer_head) {
    g_timer_head->prev = timer;
  }
  g_timer_head = timer;
}

static void js_uv_remove_timer(JSTimer *timer) {
  if (timer->prev) {
    timer->prev->next = timer->next;
  } else if (g_timer_head == timer) {
    g_timer_head = timer->next;
  }
  if (timer->next) {
    timer->next->prev = timer->prev;
  }
}

static JSTimer *js_uv_find_timer(uint32_t id) {
  for (JSTimer *cursor = g_timer_head; cursor; cursor = cursor->next) {
    if (cursor->id == id) {
      return cursor;
    }
  }
  return NULL;
}

static void js_uv_timer_close_cb(uv_handle_t *handle) {
  JSTimer *timer = (JSTimer *)handle->data;
  if (!timer) {
    return;
  }
  js_uv_remove_timer(timer);
  JS_FreeValue(timer->ctx, timer->callback);
  free(timer);
}

static void js_uv_timer_cb(uv_timer_t *handle) {
  JSTimer *timer = (JSTimer *)handle->data;
  if (!timer || !timer->ctx) {
    return;
  }
  JSContext *ctx = timer->ctx;
  JSValue result = JS_Call(ctx, timer->callback, JS_UNDEFINED, 0, NULL);
  if (JS_IsException(result)) {
    js_uv_dump_error(ctx);
  }
  JS_FreeValue(ctx, result);

  if (uv_timer_get_repeat(handle) == 0) {
    timer->closing = true;
    uv_timer_stop(handle);
    uv_close((uv_handle_t *)handle, js_uv_timer_close_cb);
  }
}

static void js_uv_sleep_close_cb(uv_handle_t *handle) {
  JSSleepRequest *request = (JSSleepRequest *)handle->data;
  if (!request) {
    return;
  }
  JSContext *ctx = request->ctx;
  JS_FreeValue(ctx, request->resolve);
  JS_FreeValue(ctx, request->reject);
  free(request);
}

static void js_uv_sleep_timer_cb(uv_timer_t *handle) {
  JSSleepRequest *request = (JSSleepRequest *)handle->data;
  if (!request || !request->ctx) {
    return;
  }
  JSContext *ctx = request->ctx;
  uv_timer_stop(handle);

  JSValue call_result = JS_Call(ctx, request->resolve, JS_UNDEFINED, 0, NULL);
  if (JS_IsException(call_result)) {
    js_uv_dump_error(ctx);
  }
  JS_FreeValue(ctx, call_result);

  uv_close((uv_handle_t *)handle, js_uv_sleep_close_cb);
}

static JSValue js_uv_set_timer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                               int magic) {
  (void)this_val;
  uv_loop_t *loop = js_uv_loop();
  if (!loop) {
    return JS_ThrowInternalError(ctx, "uv module not initialised");
  }
  if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
    return JS_ThrowTypeError(ctx, "callback must be a function");
  }

  int64_t delay = 0;
  if (argc > 1 && JS_ToInt64(ctx, &delay, argv[1])) {
    return JS_EXCEPTION;
  }
  if (delay < 0) { delay = 0; }

  JSTimer *timer = (JSTimer *)calloc(1, sizeof(*timer));
  if (!timer) {
    return JS_ThrowOutOfMemory(ctx);
  }

  timer->ctx = ctx;
  timer->callback = JS_DupValue(ctx, argv[0]);
  timer->id = g_next_timer_id++;
  if (g_next_timer_id == 0) {
    g_next_timer_id = 1;
  }

  int rc = uv_timer_init(loop, &timer->handle);
  if (rc < 0) {
    JS_FreeValue(ctx, timer->callback);
    free(timer);
    return JS_ThrowInternalError(ctx, "uv_timer_init failed: %s", uv_strerror(rc));
  }
  timer->handle.data = timer;
  js_uv_add_timer(timer);

  rc = uv_timer_start(&timer->handle, js_uv_timer_cb, (uint64_t)delay,
                      magic == JS_UV_TIMER_INTERVAL ? (uint64_t)delay : 0);
  if (rc < 0) {
    timer->closing = true;
    uv_timer_stop(&timer->handle);
    uv_close((uv_handle_t *)&timer->handle, js_uv_timer_close_cb);
    uv_run(loop, UV_RUN_NOWAIT);
    return JS_ThrowInternalError(ctx, "uv_timer_start failed: %s", uv_strerror(rc));
  }

  return JS_NewUint32(ctx, timer->id);
}

static JSValue js_uv_clear_timer(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv) {
  (void)this_val;
  if (!js_uv_loop()) {
    return JS_ThrowInternalError(ctx, "uv module not initialised");
  }
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "timer id is required");
  }

  int64_t id = 0;
  if (JS_ToInt64(ctx, &id, argv[0])) {
    return JS_EXCEPTION;
  }
  if (id <= 0) {
    return JS_UNDEFINED;
  }

  JSTimer *timer = js_uv_find_timer((uint32_t)id);
  if (!timer || timer->closing) {
    return JS_UNDEFINED;
  }

  timer->closing = true;
  uv_timer_stop(&timer->handle);
  uv_close((uv_handle_t *)&timer->handle, js_uv_timer_close_cb);
  return JS_UNDEFINED;
}

static JSValue js_uv_sleep(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  (void)this_val;
  uv_loop_t *loop = js_uv_loop();
  if (!loop) {
    return JS_ThrowInternalError(ctx, "uv module not initialised");
  }

  int64_t delay = 0;
  if (argc > 0 && JS_ToInt64(ctx, &delay, argv[0])) {
    return JS_EXCEPTION;
  }
  if (delay < 0) {
    delay = 0;
  }

  JSValue funcs[2];
  JSValue promise = JS_NewPromiseCapability(ctx, funcs);
  if (JS_IsException(promise)) {
    return promise;
  }

  JSSleepRequest *request = (JSSleepRequest *)calloc(1, sizeof(*request));
  if (!request) {
    JS_FreeValue(ctx, funcs[0]);
    JS_FreeValue(ctx, funcs[1]);
    JS_FreeValue(ctx, promise);
    return JS_ThrowOutOfMemory(ctx);
  }

  request->ctx = ctx;
  request->resolve = funcs[0];
  request->reject = funcs[1];

  int rc = uv_timer_init(loop, &request->handle);
  if (rc < 0) {
    JS_FreeValue(ctx, request->resolve);
    JS_FreeValue(ctx, request->reject);
    free(request);
    JS_FreeValue(ctx, promise);
    return JS_ThrowInternalError(ctx, "uv_timer_init failed: %s", uv_strerror(rc));
  }
  request->handle.data = request;

  rc = uv_timer_start(&request->handle, js_uv_sleep_timer_cb, (uint64_t)delay, 0);
  if (rc < 0) {
    uv_close((uv_handle_t *)&request->handle, js_uv_sleep_close_cb);
    uv_run(loop, UV_RUN_NOWAIT);
    JS_FreeValue(ctx, promise);
    return JS_ThrowInternalError(ctx, "uv_timer_start failed: %s", uv_strerror(rc));
  }

  return promise;
}

static const JSCFunctionListEntry js_uv_timer_funcs[] = {
    JS_CFUNC_MAGIC_DEF("setTimeout", 2, js_uv_set_timer, JS_UV_TIMER_ONCE),
    JS_CFUNC_MAGIC_DEF("setInterval", 2, js_uv_set_timer, JS_UV_TIMER_INTERVAL),
    JS_CFUNC_DEF("clearTimer", 1, js_uv_clear_timer),
    JS_CFUNC_DEF("sleep", 1, js_uv_sleep),
};

int js_uv_register_timers(JSContext *ctx, JSValue uv_obj) {
  JS_SetPropertyFunctionList(ctx, uv_obj, js_uv_timer_funcs, countof(js_uv_timer_funcs));
  return 0;
}
