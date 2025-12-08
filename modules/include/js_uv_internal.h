#ifndef JS_UV_INTERNAL_H
#define JS_UV_INTERNAL_H

#include "js_uv_module.h"
#include "quickjs.h"

#include <uv.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef countof
#define countof(x) (sizeof(x) / sizeof((x)[0]))
#endif

#define JS_UV_READFILE_CHUNK 65536

#ifdef _WIN32
#ifndef strdup
#define strdup _strdup
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#ifndef S_ISLNK
#ifdef S_IFLNK
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#else
#define S_ISLNK(m) 0
#endif
#endif
#else
#include <sys/stat.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif
#endif

uv_loop_t *js_uv_get_loop(void);
void js_uv_set_loop(uv_loop_t *loop);

void js_uv_dump_error(JSContext *ctx);

JSValue js_uv_make_uv_error(JSContext *ctx, int err, const char *syscall);

typedef struct JSUVPromise {
    JSContext *ctx;
    JSRuntime *rt;
    JSValue resolve;
    JSValue reject;
} JSUVPromise;

int js_uv_promise_init(JSContext *ctx, JSUVPromise *promise, JSValue *out_promise);
void js_uv_promise_destroy(JSUVPromise *promise);
void js_uv_promise_resolve(JSUVPromise *promise, JSValue value);
void js_uv_promise_resolve_undefined(JSUVPromise *promise);
void js_uv_promise_reject_uv(JSUVPromise *promise, int err, const char *syscall);
void js_uv_promise_reject_message(JSUVPromise *promise, const char *message);

typedef struct JSUVByteBuffer {
    uint8_t *data;
    size_t length;
    size_t capacity;
} JSUVByteBuffer;

void js_uv_buffer_init(JSUVByteBuffer *buf);
void js_uv_buffer_free(JSUVByteBuffer *buf);
int js_uv_buffer_append(JSUVByteBuffer *buf, const uint8_t *data, size_t length);

int js_uv_collect_data(JSContext *ctx, JSValueConst value, uint8_t **out_data, size_t *out_len);

int js_uv_register_timers(JSContext *ctx, JSValue uv_obj);
int js_uv_register_fs(JSContext *ctx, JSValue uv_obj);
int js_uv_register_dns(JSContext *ctx, JSValue uv_obj);
int js_uv_register_process(JSContext *ctx, JSValue uv_obj);
int js_uv_register_net(JSContext *ctx, JSValue uv_obj);
int js_uv_register_os(JSContext *ctx, JSValue uv_obj);
int js_uv_register_signal(JSContext *ctx, JSValue uv_obj);

static inline uv_loop_t *js_uv_loop(void) {
    return js_uv_get_loop();
}

#ifdef __cplusplus
}
#endif

#endif /* JS_UV_INTERNAL_H */
