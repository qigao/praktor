#include <stdlib.h>
#include <string.h>

#include "js_uv_internal.h"
#ifdef _WIN32
  #include <processenv.h>
#endif
typedef struct {
  JSContext *ctx;
  JSUVPromise promise;
  uv_process_t process;
  uv_pipe_t stdout_pipe;
  uv_pipe_t stderr_pipe;
  uv_stdio_container_t stdio[3];
  JSUVByteBuffer stdout_data;
  JSUVByteBuffer stderr_data;
  int exit_status;
  int term_signal;
  bool stdout_closed;
  bool stderr_closed;
  bool process_closed;
  bool failed;
} JSUVSpawn;

static void js_uv_spawn_finalize(JSUVSpawn *spawn) {
  js_uv_buffer_free(&spawn->stdout_data);
  js_uv_buffer_free(&spawn->stderr_data);
  js_uv_promise_destroy(&spawn->promise);
  free(spawn);
}

static void js_uv_spawn_maybe_finish(JSUVSpawn *spawn) {
  if (!spawn->stdout_closed || !spawn->stderr_closed || !spawn->process_closed) {
    return;
  }
  if (spawn->failed || JS_IsUndefined(spawn->promise.resolve)) {
    js_uv_spawn_finalize(spawn);
    return;
  }
  JSContext *ctx = spawn->ctx;
  JSValue result = JS_NewObject(ctx);
  if (JS_IsException(result)) {
    js_uv_promise_reject_message(&spawn->promise, "Failed to allocate result");
    js_uv_spawn_finalize(spawn);
    return;
  }
  JS_SetPropertyStr(ctx, result, "exitCode", JS_NewInt32(ctx, spawn->exit_status));
  JS_SetPropertyStr(ctx, result, "signal", JS_NewInt32(ctx, spawn->term_signal));
  JS_SetPropertyStr(
      ctx, result, "stdout",
      JS_NewStringLen(ctx, (const char *)spawn->stdout_data.data, spawn->stdout_data.length));
  JS_SetPropertyStr(
      ctx, result, "stderr",
      JS_NewStringLen(ctx, (const char *)spawn->stderr_data.data, spawn->stderr_data.length));
  js_uv_promise_resolve(&spawn->promise, result);
  js_uv_spawn_finalize(spawn);
}

static void js_uv_spawn_close_cb(uv_handle_t *handle) {
  JSUVSpawn *spawn = (JSUVSpawn *)handle->data;
  if (handle == (uv_handle_t *)&spawn->stdout_pipe) {
    spawn->stdout_closed = true;
  } else if (handle == (uv_handle_t *)&spawn->stderr_pipe) {
    spawn->stderr_closed = true;
  } else {
    spawn->process_closed = true;
  }
  js_uv_spawn_maybe_finish(spawn);
}

static void js_uv_spawn_alloc_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  JSUVSpawn *spawn = (JSUVSpawn *)handle->data;
  (void)suggested_size;
  buf->base = (char *)malloc(JS_UV_READFILE_CHUNK);
  if (!buf->base) {
    buf->len = 0;
    if (spawn) {
      spawn->failed = true;
    }
    return;
  }
  buf->len = JS_UV_READFILE_CHUNK;
}

static void js_uv_spawn_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
  JSUVSpawn *spawn = (JSUVSpawn *)stream->data;
  if (nread > 0) {
    if (stream == (uv_stream_t *)&spawn->stdout_pipe) {
      js_uv_buffer_append(&spawn->stdout_data, (const uint8_t *)buf->base, (size_t)nread);
    } else if (stream == (uv_stream_t *)&spawn->stderr_pipe) {
      js_uv_buffer_append(&spawn->stderr_data, (const uint8_t *)buf->base, (size_t)nread);
    }
  } else if (nread < 0 && nread != UV_EOF) {
    spawn->failed = true;
    js_uv_promise_reject_uv(&spawn->promise, (int)nread, "uv_read");
    uv_read_stop(stream);
    uv_close((uv_handle_t *)stream, js_uv_spawn_close_cb);
    uv_stream_t *other = stream == (uv_stream_t *)&spawn->stdout_pipe
                             ? (uv_stream_t *)&spawn->stderr_pipe
                             : (uv_stream_t *)&spawn->stdout_pipe;
    if (!uv_is_closing((uv_handle_t *)other)) {
      uv_close((uv_handle_t *)other, js_uv_spawn_close_cb);
    }
  }
  free(buf->base);
  if (nread == UV_EOF) {
    uv_read_stop(stream);
    uv_close((uv_handle_t *)stream, js_uv_spawn_close_cb);
  }
}

static void js_uv_spawn_exit_cb(uv_process_t *proc, int64_t exit_status, int term_signal) {
  JSUVSpawn *spawn = (JSUVSpawn *)proc->data;
  spawn->exit_status = (int)exit_status;
  spawn->term_signal = term_signal;
  uv_close((uv_handle_t *)proc, js_uv_spawn_close_cb);
}

static void js_uv_free_str_array(char **array) {
  if (!array) {
    return;
  }
  for (char **p = array; *p; ++p) {
    free(*p);
  }
  free(array);
}

static char **js_uv_build_str_array(JSContext *ctx, JSValueConst js_array, bool include_program,
                                    const char *program) {
  if (!JS_IsArray(js_array)) {
    return NULL;
  }
  int64_t len64 = 0;
  if (JS_GetLength(ctx, js_array, &len64) < 0 || len64 < 0) {
    return NULL;
  }
  uint32_t length = (uint32_t)len64;
  if ((int64_t)length != len64) {
    JS_ThrowRangeError(ctx, "argument list too large");
    return NULL;
  }
  uint32_t count = length + (include_program ? 1 : 0) + 1;
  char **result = (char **)calloc(count, sizeof(char *));
  if (!result) {
    JS_ThrowOutOfMemory(ctx);
    return NULL;
  }
  uint32_t index = 0;
  if (include_program) {
    result[index++] = strdup(program);
  }
  for (uint32_t i = 0; i < length; ++i) {
    JSValue val = JS_GetPropertyUint32(ctx, js_array, i);
    if (JS_IsUndefined(val)) {
      JS_FreeValue(ctx, val);
      continue;
    }
    const char *str = JS_ToCString(ctx, val);
    if (!str) {
      JS_FreeValue(ctx, val);
      js_uv_free_str_array(result);
      return NULL;
    }
    result[index++] = strdup(str);
    JS_FreeCString(ctx, str);
    JS_FreeValue(ctx, val);
  }
  result[index] = NULL;
  return result;
}

static JSValue js_uv_spawn(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  (void)this_val;
  if (!js_uv_loop()) {
    return JS_ThrowInternalError(ctx, "uv module not initialised");
  }
  if (argc < 1 || !JS_IsObject(argv[0])) {
    return JS_ThrowTypeError(ctx, "options object required");
  }
  JSValueConst options = argv[0];
  JSValue val = JS_GetPropertyStr(ctx, options, "file");
  if (JS_IsUndefined(val)) {
    JS_FreeValue(ctx, val);
    return JS_ThrowTypeError(ctx, "options.file is required");
  }
  const char *file = JS_ToCString(ctx, val);
  if (!file) {
    JS_FreeValue(ctx, val);
    return JS_EXCEPTION;
  }
  JSUVSpawn *spawn = (JSUVSpawn *)calloc(1, sizeof(*spawn));
  if (!spawn) {
    JS_FreeCString(ctx, file);
    JS_FreeValue(ctx, val);
    return JS_ThrowOutOfMemory(ctx);
  }
  spawn->ctx = ctx;
  js_uv_buffer_init(&spawn->stdout_data);
  js_uv_buffer_init(&spawn->stderr_data);
  spawn->process.data = spawn;
  spawn->stdout_pipe.data = spawn;
  spawn->stderr_pipe.data = spawn;
  JSValue promise;
  if (js_uv_promise_init(ctx, &spawn->promise, &promise) != 0) {
    JS_FreeCString(ctx, file);
    JS_FreeValue(ctx, val);
    free(spawn);
    return JS_EXCEPTION;
  }
  JSValue args_array = JS_GetPropertyStr(ctx, options, "args");
  char **args_list = NULL;
  if (!JS_IsUndefined(args_array)) {
    args_list = js_uv_build_str_array(ctx, args_array, true, file);
    if (!args_list) {
      JS_FreeValue(ctx, args_array);
      JS_FreeCString(ctx, file);
      JS_FreeValue(ctx, val);
      js_uv_promise_destroy(&spawn->promise);
      free(spawn);
      return JS_EXCEPTION;
    }
  }
  JS_FreeValue(ctx, args_array);
  JSValue env_obj = JS_GetPropertyStr(ctx, options, "env");
  char **env_list = NULL;
  if (JS_IsObject(env_obj)) {
    JSPropertyEnum *props = NULL;
    uint32_t prop_count = 0;
    if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, env_obj,
                               JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK) < 0) {
      JS_FreeValue(ctx, env_obj);
      JS_FreeCString(ctx, file);
      JS_FreeValue(ctx, val);
      js_uv_promise_destroy(&spawn->promise);
      js_uv_free_str_array(args_list);
      free(spawn);
      return JS_EXCEPTION;
    }
    env_list = (char **)calloc(prop_count + 1, sizeof(char *));
    if (!env_list) {
      JS_FreePropertyEnum(ctx, props, prop_count);
      JS_FreeValue(ctx, env_obj);
      JS_FreeCString(ctx, file);
      JS_FreeValue(ctx, val);
      js_uv_promise_destroy(&spawn->promise);
      js_uv_free_str_array(args_list);
      free(spawn);
      return JS_ThrowOutOfMemory(ctx);
    }
    uint32_t env_index = 0;
    bool env_ok = true;
    for (uint32_t i = 0; i < prop_count && env_ok; ++i) {
      JSAtom atom = props[i].atom;
      const char *key_str = JS_AtomToCString(ctx, atom);
      if (!key_str) {
        JS_FreeAtom(ctx, atom);
        env_ok = false;
        break;
      }
      JSValue env_val = JS_GetProperty(ctx, env_obj, atom);
      const char *env_str = JS_ToCString(ctx, env_val);
      if (!env_str) {
        JS_FreeCString(ctx, key_str);
        JS_FreeValue(ctx, env_val);
        JS_FreeAtom(ctx, atom);
        env_ok = false;
        break;
      }
      size_t pair_len = strlen(key_str) + strlen(env_str) + 2;
      char *pair = (char *)malloc(pair_len);
      if (!pair) {
        JS_FreeCString(ctx, key_str);
        JS_FreeCString(ctx, env_str);
        JS_FreeValue(ctx, env_val);
        JS_FreeAtom(ctx, atom);
        JS_ThrowOutOfMemory(ctx);
        env_ok = false;
        break;
      }
      snprintf(pair, pair_len, "%s=%s", key_str, env_str);
      env_list[env_index++] = pair;
      JS_FreeCString(ctx, key_str);
      JS_FreeCString(ctx, env_str);
      JS_FreeValue(ctx, env_val);
      JS_FreeAtom(ctx, atom);
    }
    JS_FreePropertyEnum(ctx, props, prop_count);
    if (!env_ok) {
      js_uv_free_str_array(env_list);
      JS_FreeValue(ctx, env_obj);
      JS_FreeCString(ctx, file);
      JS_FreeValue(ctx, val);
      js_uv_promise_destroy(&spawn->promise);
      js_uv_free_str_array(args_list);
      free(spawn);
      return JS_EXCEPTION;
    }
    env_list[env_index] = NULL;
  }
  JS_FreeValue(ctx, env_obj);
  const char *cwd = NULL;
  JSValue cwd_val = JS_GetPropertyStr(ctx, options, "cwd");
  if (!JS_IsUndefined(cwd_val)) {
    cwd = JS_ToCString(ctx, cwd_val);
  }
  uv_process_options_t proc_options;
  memset(&proc_options, 0, sizeof(proc_options));
  proc_options.exit_cb = js_uv_spawn_exit_cb;
  proc_options.file = file;
  proc_options.args = args_list ? args_list : (char *[]){(char *)file, NULL};
  proc_options.env = env_list;
  proc_options.cwd = cwd;
  uv_pipe_init(js_uv_loop(), &spawn->stdout_pipe, 0);
  uv_pipe_init(js_uv_loop(), &spawn->stderr_pipe, 0);
  spawn->stdio[0].flags = UV_IGNORE;
  spawn->stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  spawn->stdio[1].data.stream = (uv_stream_t *)&spawn->stdout_pipe;
  spawn->stdio[2].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  spawn->stdio[2].data.stream = (uv_stream_t *)&spawn->stderr_pipe;
  proc_options.stdio = spawn->stdio;
  proc_options.stdio_count = 3;
  int rc = uv_spawn(js_uv_loop(), &spawn->process, &proc_options);
  JS_FreeCString(ctx, file);
  JS_FreeValue(ctx, val);
  if (cwd) {
    JS_FreeCString(ctx, cwd);
  }
  JS_FreeValue(ctx, cwd_val);
  if (rc < 0) {
    spawn->failed = true;
    js_uv_promise_reject_uv(&spawn->promise, rc, "uv_spawn");
    if (!uv_is_closing((uv_handle_t *)&spawn->stdout_pipe)) {
      uv_close((uv_handle_t *)&spawn->stdout_pipe, js_uv_spawn_close_cb);
    }
    if (!uv_is_closing((uv_handle_t *)&spawn->stderr_pipe)) {
      uv_close((uv_handle_t *)&spawn->stderr_pipe, js_uv_spawn_close_cb);
    }
    spawn->process_closed = true;
    JS_FreeValue(ctx, promise);
    js_uv_free_str_array(args_list);
    js_uv_free_str_array(env_list);
    return JS_EXCEPTION;
  }
  js_uv_free_str_array(args_list);
  js_uv_free_str_array(env_list);
  spawn->stdout_pipe.data = spawn;
  spawn->stderr_pipe.data = spawn;
  uv_read_start((uv_stream_t *)&spawn->stdout_pipe, js_uv_spawn_alloc_cb, js_uv_spawn_read_cb);
  uv_read_start((uv_stream_t *)&spawn->stderr_pipe, js_uv_spawn_alloc_cb, js_uv_spawn_read_cb);
  return promise;
}

static const JSCFunctionListEntry js_uv_spawn_funcs[] = {
    JS_CFUNC_DEF("spawn", 1, js_uv_spawn),
};

int js_uv_register_process(JSContext *ctx, JSValue uv_obj) {
  JSValue proc_obj = JS_NewObject(ctx);
  if (JS_IsException(proc_obj)) {
    return -1;
  }
  JS_SetPropertyFunctionList(ctx, proc_obj, js_uv_spawn_funcs, countof(js_uv_spawn_funcs));
  if (JS_SetPropertyStr(ctx, uv_obj, "proc", proc_obj) < 0) {
    JS_FreeValue(ctx, proc_obj);
    return -1;
  }
  return 0;
}
