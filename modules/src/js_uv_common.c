#include "js_uv_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static uv_loop_t *g_js_uv_loop = NULL;

uv_loop_t *js_uv_get_loop(void) {
    return g_js_uv_loop;
}

void js_uv_set_loop(uv_loop_t *loop) {
    g_js_uv_loop = loop;
}

void js_uv_dump_error(JSContext *ctx) {    JSValue exception = JS_GetException(ctx);
    if (JS_IsException(exception)) {
        return;
    }
    const char *message = JS_ToCString(ctx, exception);
    if (message) {
        fprintf(stderr, "%s\n", message);
        JS_FreeCString(ctx, message);
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
    JS_FreeValue(ctx, exception);
}

JSValue js_uv_make_uv_error(JSContext *ctx, int err, const char *syscall) {
    JSValue error = JS_NewError(ctx);
    if (JS_IsException(error)) {
        return error;
    }
    const char *sys = syscall ? syscall : "uv";
    JSValue message = JS_NewString(ctx, uv_strerror(err));
    if (!JS_IsException(message)) {
        JS_SetPropertyStr(ctx, error, "message", message);
    }
    JS_SetPropertyStr(ctx, error, "code", JS_NewString(ctx, uv_err_name(err)));
    if (syscall) {
        JS_SetPropertyStr(ctx, error, "syscall", JS_NewString(ctx, sys));
    }
    return error;
}

int js_uv_promise_init(JSContext *ctx, JSUVPromise *promise, JSValue *out_promise) {
    JSValue funcs[2];
    JSValue promise_value = JS_NewPromiseCapability(ctx, funcs);
    if (JS_IsException(promise_value)) {
        return -1;
    }
    promise->ctx = ctx;
    promise->rt = JS_GetRuntime(ctx);
    promise->resolve = funcs[0];
    promise->reject = funcs[1];
    if (out_promise) {
        *out_promise = promise_value;
    } else {
        JS_FreeValue(ctx, promise_value);
    }
    return 0;
}

void js_uv_promise_destroy(JSUVPromise *promise) {
    if (!promise->ctx) {
        return;
    }
    JS_FreeValue(promise->ctx, promise->resolve);
    JS_FreeValue(promise->ctx, promise->reject);
    promise->ctx = NULL;
    promise->rt = NULL;
    promise->resolve = JS_UNDEFINED;
    promise->reject = JS_UNDEFINED;
}

void js_uv_promise_resolve(JSUVPromise *promise, JSValue value) {
    if (!promise->ctx) {
        if (promise->rt) {
            JS_FreeValueRT(promise->rt, value);
        }
        return;
    }
    JSContext *ctx = promise->ctx;
    JSValue argv[1] = { value };
    JSValue result = JS_Call(ctx, promise->resolve, JS_UNDEFINED, 1, argv);
    if (JS_IsException(result)) {
        js_uv_dump_error(ctx);
    }
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, argv[0]);
    js_uv_promise_destroy(promise);
}

void js_uv_promise_resolve_undefined(JSUVPromise *promise) {
    js_uv_promise_resolve(promise, JS_UNDEFINED);
}

void js_uv_promise_reject_uv(JSUVPromise *promise, int err, const char *syscall) {
    if (!promise->ctx) {
        return;
    }
    JSContext *ctx = promise->ctx;
    JSValue error = js_uv_make_uv_error(ctx, err, syscall);
    JSValue argv[1] = { error };
    JSValue result = JS_Call(ctx, promise->reject, JS_UNDEFINED, 1, argv);
    if (JS_IsException(result)) {
        js_uv_dump_error(ctx);
    }
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, argv[0]);
    js_uv_promise_destroy(promise);
}

void js_uv_promise_reject_message(JSUVPromise *promise, const char *message) {
    if (!promise->ctx) {
        return;
    }
    JSContext *ctx = promise->ctx;
    JSValue error = JS_NewError(ctx);
    if (!JS_IsException(error) && message) {
        JS_SetPropertyStr(ctx, error, "message", JS_NewString(ctx, message));
    }
    JSValue argv[1] = { error };
    JSValue result = JS_Call(ctx, promise->reject, JS_UNDEFINED, 1, argv);
    if (JS_IsException(result)) {
        js_uv_dump_error(ctx);
    }
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, argv[0]);
    js_uv_promise_destroy(promise);
}

void js_uv_buffer_init(JSUVByteBuffer *buf) {
    buf->data = NULL;
    buf->length = 0;
    buf->capacity = 0;
}

void js_uv_buffer_free(JSUVByteBuffer *buf) {
    if (buf->data) {
        free(buf->data);
    }
    buf->data = NULL;
    buf->length = 0;
    buf->capacity = 0;
}

static int js_uv_buffer_reserve(JSUVByteBuffer *buf, size_t additional) {
    size_t required = buf->length + additional;
    if (required <= buf->capacity) {
        return 0;
    }
    size_t new_capacity = buf->capacity ? buf->capacity : JS_UV_READFILE_CHUNK;
    while (new_capacity < required) {
        size_t next = new_capacity * 2;
        if (next < new_capacity) {
            return -1;
        }
        new_capacity = next;
    }
    uint8_t *tmp = (uint8_t *)realloc(buf->data, new_capacity);
    if (!tmp) {
        return -1;
    }
    buf->data = tmp;
    buf->capacity = new_capacity;
    return 0;
}

int js_uv_buffer_append(JSUVByteBuffer *buf, const uint8_t *data, size_t length) {
    if (length == 0) {
        return 0;
    }
    if (js_uv_buffer_reserve(buf, length) != 0) {
        return -1;
    }
    memcpy(buf->data + buf->length, data, length);
    buf->length += length;
    return 0;
}

int js_uv_collect_data(JSContext *ctx, JSValueConst value, uint8_t **out_data, size_t *out_len) {
    if (JS_IsString(value)) {
        size_t len = 0;
        const char *str = JS_ToCStringLen(ctx, &len, value);
        if (!str) {
            return -1;
        }
        uint8_t *copy = (uint8_t *)malloc(len);
        if (!copy) {
            JS_FreeCString(ctx, str);
            JS_ThrowOutOfMemory(ctx);
            return -1;
        }
        memcpy(copy, str, len);
        JS_FreeCString(ctx, str);
        *out_data = copy;
        *out_len = len;
        return 0;
    }

    size_t size = 0;
    uint8_t *buf = JS_GetArrayBuffer(ctx, &size, value);
    if (buf) {
        uint8_t *copy = (uint8_t *)malloc(size);
        if (!copy) {
            JS_ThrowOutOfMemory(ctx);
            return -1;
        }
        memcpy(copy, buf, size);
        *out_data = copy;
        *out_len = size;
        return 0;
    }

    size_t offset = 0;
    size_t length = 0;
    size_t stride = 0;
    JSValue array_buffer = JS_GetTypedArrayBuffer(ctx, value, &offset, &length, &stride);
    if (JS_IsException(array_buffer)) {
        JS_ThrowTypeError(ctx, "data must be string, ArrayBuffer, or TypedArray");
        return -1;
    }
    if (JS_IsNull(array_buffer)) {
        JS_ThrowTypeError(ctx, "data must be string, ArrayBuffer, or TypedArray");
        return -1;
    }
    size_t backing_size = 0;
    uint8_t *backing = JS_GetArrayBuffer(ctx, &backing_size, array_buffer);
    if (!backing || offset + length > backing_size) {
        JS_FreeValue(ctx, array_buffer);
        JS_ThrowTypeError(ctx, "typed array backing buffer is invalid");
        return -1;
    }
    uint8_t *copy = (uint8_t *)malloc(length);
    if (!copy) {
        JS_FreeValue(ctx, array_buffer);
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }
    memcpy(copy, backing + offset, length);
    JS_FreeValue(ctx, array_buffer);
    *out_data = copy;
    *out_len = length;
    return 0;
}
