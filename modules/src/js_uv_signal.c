#include <stdlib.h>

#include "js_uv_internal.h"
static JSClassID js_uv_signal_class_id = 0;

typedef struct {
  JSContext *ctx;
  JSValue obj_ref;
  JSValue callback;
  JSValue on_close;
  uv_signal_t handle;
  int closing;
} JSUVSignal;

typedef struct JSUVSignalRuntime {
  JSRuntime *rt;
  struct JSUVSignalRuntime *next;
} JSUVSignalRuntime;

static JSUVSignalRuntime *js_uv_signal_runtimes = NULL;

static JSUVSignal *js_uv_get_signal(JSContext *ctx, JSValueConst this_val) {
  return (JSUVSignal *)JS_GetOpaque2(ctx, this_val, js_uv_signal_class_id);
}

static void js_uv_signal_finalize(JSContext *ctx, JSUVSignal *sig) {
  if (!sig) {
    return;
  }
  JS_FreeValue(ctx, sig->callback);
  JS_FreeValue(ctx, sig->on_close);
  JS_FreeValue(ctx, sig->obj_ref);
  free(sig);
}

static void js_uv_signal_close(JSUVSignal *sig);

static void js_uv_signal_close_cb(uv_handle_t *handle) {
  JSUVSignal *sig = (JSUVSignal *)handle->data;
  if (!sig) {
    return;
  }
  JSContext *ctx = sig->ctx;
  if (!JS_IsUndefined(sig->on_close)) {
    JSValue result = JS_Call(ctx, sig->on_close, JS_UNDEFINED, 0, NULL);
    if (JS_IsException(result)) {
      js_uv_dump_error(ctx);
    }
    JS_FreeValue(ctx, result);
  }
  JS_SetOpaque(sig->obj_ref, NULL);
  js_uv_signal_finalize(ctx, sig);
}

static void js_uv_signal_close(JSUVSignal *sig) {
  if (sig && !sig->closing) {
    sig->closing = 1;
    uv_signal_stop(&sig->handle);
    uv_close((uv_handle_t *)&sig->handle, js_uv_signal_close_cb);
  }
}

static void js_uv_signal_cb(uv_signal_t *handle, int signum) {
  JSUVSignal *sig = (JSUVSignal *)handle->data;
  if (!sig || JS_IsUndefined(sig->callback)) {
    return;
  }
  JSContext *ctx = sig->ctx;
  JSValue argv[1] = {JS_NewInt32(ctx, signum)};
  JSValue result = JS_Call(ctx, sig->callback, JS_UNDEFINED, 1, argv);
  if (JS_IsException(result)) {
    js_uv_dump_error(ctx);
  }
  JS_FreeValue(ctx, result);
  JS_FreeValue(ctx, argv[0]);
}

static JSValue js_uv_signal_start(JSContext *ctx, JSValueConst this_val, int argc,
                                  JSValueConst *argv) {
  (void)this_val;
  if (!js_uv_loop()) {
    return JS_ThrowInternalError(ctx, "uv module not initialised");
  }
  if (argc < 2 || !JS_IsNumber(argv[0]) || !JS_IsFunction(ctx, argv[1])) {
    return JS_ThrowTypeError(ctx, "signum and callback required");
  }
  int32_t signum = 0;
  if (JS_ToInt32(ctx, &signum, argv[0])) {
    return JS_EXCEPTION;
  }
  JSValue obj = JS_NewObjectClass(ctx, js_uv_signal_class_id);
  if (JS_IsException(obj)) {
    return obj;
  }
  JSUVSignal *sig = (JSUVSignal *)calloc(1, sizeof(*sig));
  if (!sig) {
    JS_FreeValue(ctx, obj);
    return JS_ThrowOutOfMemory(ctx);
  }
  sig->ctx = ctx;
  sig->obj_ref = JS_DupValue(ctx, obj);
  sig->callback = JS_DupValue(ctx, argv[1]);
  sig->on_close = JS_UNDEFINED;
  sig->closing = 0;
  if (uv_signal_init(js_uv_loop(), &sig->handle) != 0) {
    JS_FreeValue(ctx, sig->callback);
    JS_FreeValue(ctx, sig->obj_ref);
    JS_FreeValue(ctx, obj);
    free(sig);
    return JS_ThrowInternalError(ctx, "uv_signal_init failed");
  }
  sig->handle.data = sig;
  if (uv_signal_start(&sig->handle, js_uv_signal_cb, signum) != 0) {
    sig->closing = 1;
    uv_signal_stop(&sig->handle);
    uv_close((uv_handle_t *)&sig->handle, js_uv_signal_close_cb);
    JS_FreeValue(ctx, obj);
    return JS_ThrowInternalError(ctx, "uv_signal_start failed");
  }
  JS_SetOpaque(obj, sig);
  return obj;
}

static JSValue js_uv_signal_on_close(JSContext *ctx, JSValueConst this_val, int argc,
                                     JSValueConst *argv) {
  if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
    return JS_ThrowTypeError(ctx, "callback required");
  }
  JSUVSignal *sig = js_uv_get_signal(ctx, this_val);
  if (!sig) {
    return JS_EXCEPTION;
  }
  JS_FreeValue(ctx, sig->on_close);
  sig->on_close = JS_DupValue(ctx, argv[0]);
  return JS_UNDEFINED;
}

static JSValue js_uv_signal_stop(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv) {
  (void)argc;
  (void)argv;
  JSUVSignal *sig = js_uv_get_signal(ctx, this_val);
  if (!sig) {
    return JS_EXCEPTION;
  }
  js_uv_signal_close(sig);
  return JS_UNDEFINED;
}

static void js_uv_signal_finalizer(JSRuntime *rt, JSValue val) {
  (void)rt;
  JSUVSignal *sig = (JSUVSignal *)JS_GetOpaque(val, js_uv_signal_class_id);
  if (!sig) {
    return;
  }
  js_uv_signal_close(sig);
}

static const JSCFunctionListEntry js_uv_signal_proto_funcs[] = {
    JS_CFUNC_DEF("stop", 0, js_uv_signal_stop),
    JS_CFUNC_DEF("onClose", 1, js_uv_signal_on_close),
};
static const JSCFunctionListEntry js_uv_signal_funcs[] = {
    JS_CFUNC_DEF("watch", 2, js_uv_signal_start),
};

int js_uv_register_signal(JSContext *ctx, JSValue uv_obj) {
  JSRuntime *rt = JS_GetRuntime(ctx);
  JS_NewClassID(rt, &js_uv_signal_class_id);
  JSClassDef def = {"uv.Signal", .finalizer = js_uv_signal_finalizer};
  if (JS_NewClass(rt, js_uv_signal_class_id, &def) < 0) {
    return -1;
  }
  JSValue proto = JS_NewObject(ctx);
  if (JS_IsException(proto)) {
    return -1;
  }
  JS_SetPropertyFunctionList(ctx, proto, js_uv_signal_proto_funcs,
                             countof(js_uv_signal_proto_funcs));
  JS_SetClassProto(ctx, js_uv_signal_class_id, proto);
  JSValue signal_obj = JS_NewObject(ctx);
  if (JS_IsException(signal_obj)) {
    return -1;
  }
  JS_SetPropertyFunctionList(ctx, signal_obj, js_uv_signal_funcs, countof(js_uv_signal_funcs));
  if (JS_SetPropertyStr(ctx, uv_obj, "signal", signal_obj) < 0) {
    JS_FreeValue(ctx, signal_obj);
    return -1;
  }
  return 0;
}
