#include "js_uv_internal.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <errno.h>
  #include <sys/resource.h>
#endif

static JSValue js_uv_os_hostname(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;

  char buf[256];
  size_t len = sizeof(buf);
  int rc = uv_os_gethostname(buf, &len);
  if (rc == UV_ENOBUFS) {
    char *dyn = (char *)malloc(len);
    if (!dyn) {
      return JS_ThrowOutOfMemory(ctx);
    }
    rc = uv_os_gethostname(dyn, &len);
    if (rc < 0) {
      JSValue err = js_uv_make_uv_error(ctx, rc, "uv_os_gethostname");
      free(dyn);
      return JS_Throw(ctx, err);
    }
    JSValue result = JS_NewStringLen(ctx, dyn, len);
    free(dyn);
    return result;
  }
  if (rc < 0) {
    return JS_Throw(ctx, js_uv_make_uv_error(ctx, rc, "uv_os_gethostname"));
  }
  return JS_NewStringLen(ctx, buf, len);
}

static JSValue js_uv_os_homedir(JSContext *ctx, JSValueConst this_val, int argc,
                                JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;

  char buf[512];
  size_t len = sizeof(buf);
  int rc = uv_os_homedir(buf, &len);
  if (rc == UV_ENOBUFS) {
    char *dyn = (char *)malloc(len);
    if (!dyn) {
      return JS_ThrowOutOfMemory(ctx);
    }
    rc = uv_os_homedir(dyn, &len);
    if (rc < 0) {
      JSValue err = js_uv_make_uv_error(ctx, rc, "uv_os_homedir");
      free(dyn);
      return JS_Throw(ctx, err);
    }
    JSValue result = JS_NewStringLen(ctx, dyn, len);
    free(dyn);
    return result;
  }
  if (rc < 0) {
    return JS_Throw(ctx, js_uv_make_uv_error(ctx, rc, "uv_os_homedir"));
  }
  return JS_NewStringLen(ctx, buf, len);
}

static JSValue js_uv_os_tmpdir(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;

  char buf[512];
  size_t len = sizeof(buf);
  int rc = uv_os_tmpdir(buf, &len);
  if (rc == UV_ENOBUFS) {
    char *dyn = (char *)malloc(len);
    if (!dyn) {
      return JS_ThrowOutOfMemory(ctx);
    }
    rc = uv_os_tmpdir(dyn, &len);
    if (rc < 0) {
      JSValue err = js_uv_make_uv_error(ctx, rc, "uv_os_tmpdir");
      free(dyn);
      return JS_Throw(ctx, err);
    }
    JSValue result = JS_NewStringLen(ctx, dyn, len);
    free(dyn);
    return result;
  }
  if (rc < 0) {
    return JS_Throw(ctx, js_uv_make_uv_error(ctx, rc, "uv_os_tmpdir"));
  }
  return JS_NewStringLen(ctx, buf, len);
}

static JSValue js_uv_os_uptime(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;
  double uptime = 0;
  int rc = uv_uptime(&uptime);
  if (rc < 0) {
    return JS_Throw(ctx, js_uv_make_uv_error(ctx, rc, "uv_uptime"));
  }
  return JS_NewFloat64(ctx, uptime);
}

static JSValue js_uv_os_loadavg(JSContext *ctx, JSValueConst this_val, int argc,
                                JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;
  double avg[3];
  uv_loadavg(avg);
  JSValue arr = JS_NewArray(ctx);
  JS_DefinePropertyValueUint32(ctx, arr, 0, JS_NewFloat64(ctx, avg[0]), JS_PROP_C_W_E);
  JS_DefinePropertyValueUint32(ctx, arr, 1, JS_NewFloat64(ctx, avg[1]), JS_PROP_C_W_E);
  JS_DefinePropertyValueUint32(ctx, arr, 2, JS_NewFloat64(ctx, avg[2]), JS_PROP_C_W_E);
  return arr;
}

static JSValue js_uv_os_totalmem(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;
  uint64_t mem = uv_get_total_memory();
#ifdef CONFIG_BIGNUM
  return JS_NewBigUint64(ctx, mem);
#else
  return JS_NewFloat64(ctx, (double)mem);
#endif
}

static JSValue js_uv_os_freemem(JSContext *ctx, JSValueConst this_val, int argc,
                                JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;
  uint64_t mem = uv_get_free_memory();
#ifdef CONFIG_BIGNUM
  return JS_NewBigUint64(ctx, mem);
#else
  return JS_NewFloat64(ctx, (double)mem);
#endif
}

static JSValue js_uv_os_getenv(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  if (argc < 1 || !JS_IsString(argv[0])) {
    return JS_ThrowTypeError(ctx, "variable name required");
  }
  size_t namelen;
  const char *name = JS_ToCStringLen(ctx, &namelen, argv[0]);
  if (!name) {
    return JS_EXCEPTION;
  }
  char buf[256];
  size_t len = sizeof(buf);
  int rc = uv_os_getenv(name, buf, &len);
  JS_FreeCString(ctx, name);
  if (rc == UV_ENOBUFS) {
    char *dyn = (char *)malloc(len);
    if (!dyn) {
      return JS_ThrowOutOfMemory(ctx);
    }
    // Need name again
    name = JS_ToCStringLen(ctx, &namelen, argv[0]);
    if (!name) {
      free(dyn);
      return JS_EXCEPTION;
    }
    rc = uv_os_getenv(name, dyn, &len);
    JS_FreeCString(ctx, name);
    if (rc < 0) {
      JSValue err = js_uv_make_uv_error(ctx, rc, "uv_os_getenv");
      free(dyn);
      return JS_Throw(ctx, err);
    }
    JSValue result = JS_NewStringLen(ctx, dyn, len);
    free(dyn);
    return result;
  }
  if (rc == UV_ENOENT) {
    return JS_UNDEFINED;
  }
  if (rc < 0) {
    return JS_Throw(ctx, js_uv_make_uv_error(ctx, rc, "uv_os_getenv"));
  }
  return JS_NewStringLen(ctx, buf, len);
}

static JSValue js_uv_os_cpu_info(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv) {
  (void)this_val;
  (void)argc;
  (void)argv;
  uv_cpu_info_t *info = NULL;
  int count = 0;
  int rc = uv_cpu_info(&info, &count);
  if (rc < 0) {
    return JS_Throw(ctx, js_uv_make_uv_error(ctx, rc, "uv_cpu_info"));
  }
  JSValue arr = JS_NewArray(ctx);
  for (int i = 0; i < count; ++i) {
    JSValue item = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, item, "model", JS_NewString(ctx, info[i].model));
    JS_SetPropertyStr(ctx, item, "speed", JS_NewInt32(ctx, info[i].speed));
    JSValue times = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, times, "user", JS_NewFloat64(ctx, info[i].cpu_times.user));
    JS_SetPropertyStr(ctx, times, "nice", JS_NewFloat64(ctx, info[i].cpu_times.nice));
    JS_SetPropertyStr(ctx, times, "sys", JS_NewFloat64(ctx, info[i].cpu_times.sys));
    JS_SetPropertyStr(ctx, times, "idle", JS_NewFloat64(ctx, info[i].cpu_times.idle));
    JS_SetPropertyStr(ctx, times, "irq", JS_NewFloat64(ctx, info[i].cpu_times.irq));
    JS_SetPropertyStr(ctx, item, "times", times);
    JS_DefinePropertyValueUint32(ctx, arr, (uint32_t)i, item, JS_PROP_C_W_E);
  }
  uv_free_cpu_info(info, count);
  return arr;
}

#ifdef _WIN32
static JSValue js_uv_os_getpriority(JSContext *ctx, JSValueConst this_val, int argc,
                                    JSValueConst *argv) {
  (void)this_val;
  HANDLE process = GetCurrentProcess();
  if (argc > 0 && JS_IsNumber(argv[0])) {
    int32_t pid;
    if (JS_ToInt32(ctx, &pid, argv[0])) {
      return JS_EXCEPTION;
    }
    process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
    if (!process) {
      return JS_Throw(
          ctx, js_uv_make_uv_error(ctx, uv_translate_sys_error(GetLastError()), "OpenProcess"));
    }
  }
  int priority = GetPriorityClass(process);
  if (!priority) {
    if (process != GetCurrentProcess()) {
      CloseHandle(process);
    }
    return JS_Throw(
        ctx, js_uv_make_uv_error(ctx, uv_translate_sys_error(GetLastError()), "GetPriorityClass"));
  }
  if (process != GetCurrentProcess()) {
    CloseHandle(process);
  }
  return JS_NewInt32(ctx, priority);
}
#else
static JSValue js_uv_os_getpriority(JSContext *ctx, JSValueConst this_val, int argc,
                                    JSValueConst *argv) {
  (void)this_val;
  int which = PRIO_PROCESS;
  int who = 0;
  if (argc > 0 && JS_IsNumber(argv[0])) {
    if (JS_ToInt32(ctx, &who, argv[0])) {
      return JS_EXCEPTION;
    }
  }
  errno = 0;
  int prio = getpriority(which, who);
  if (prio == -1 && errno != 0) {
    return JS_Throw(ctx, js_uv_make_uv_error(ctx, uv_translate_sys_error(errno), "getpriority"));
  }
  return JS_NewInt32(ctx, prio);
}
#endif

static const JSCFunctionListEntry js_uv_os_funcs[] = {
    JS_CFUNC_DEF("hostname", 0, js_uv_os_hostname),
    JS_CFUNC_DEF("homedir", 0, js_uv_os_homedir),
    JS_CFUNC_DEF("tmpdir", 0, js_uv_os_tmpdir),
    JS_CFUNC_DEF("uptime", 0, js_uv_os_uptime),
    JS_CFUNC_DEF("loadavg", 0, js_uv_os_loadavg),
    JS_CFUNC_DEF("totalmem", 0, js_uv_os_totalmem),
    JS_CFUNC_DEF("freemem", 0, js_uv_os_freemem),
    JS_CFUNC_DEF("getenv", 1, js_uv_os_getenv),
    JS_CFUNC_DEF("cpus", 0, js_uv_os_cpu_info),
    JS_CFUNC_DEF("getPriority", 1, js_uv_os_getpriority),
};

int js_uv_register_os(JSContext *ctx, JSValue uv_obj) {
  JSValue os_obj = JS_NewObject(ctx);
  if (JS_IsException(os_obj)) {
    return -1;
  }
  JS_SetPropertyFunctionList(ctx, os_obj, js_uv_os_funcs, countof(js_uv_os_funcs));
  if (JS_SetPropertyStr(ctx, uv_obj, "os", os_obj) < 0) {
    JS_FreeValue(ctx, os_obj);
    return -1;
  }
  return 0;
}
