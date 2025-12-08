#include "quickjs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#if defined(__APPLE__)
  #include <malloc/malloc.h>
#else
  #include <malloc.h>
#endif

typedef struct JSMallocState JSMallocState;

void *sks_trace_malloc(JSMallocState *state, size_t size) { return malloc(size); }

void sks_trace_free(JSMallocState *state, void *ptr) { free(ptr); }

void *sks_trace_realloc(JSMallocState *state, void *ptr, size_t size) {
  if (!ptr)
    return malloc(size);
  if (!size) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, size);
}

JSValue js_print_to_console(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  int i;
  const char *str;

  for (i = 0; i < argc; i++) {
    str = JS_ToCString(ctx, argv[i]);
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

void init_c_hooks(JSContext *ctx) {
  JSValue global_obj = JS_GetGlobalObject(ctx);
  JSValue console_obj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, console_obj, "log", JS_NewCFunction(ctx, js_print_to_console, "log", 1));
  JS_SetPropertyStr(ctx, global_obj, "console", console_obj);

  JS_FreeValue(ctx, global_obj);
}

void dump_value_to_stream(JSContext *ctx, FILE *stream, JSValueConst val) {
  const char *strval = JS_ToCString(ctx, val);
  if (strval) {
    fprintf(stderr, "%s\n", strval);
    JS_FreeCString(ctx, strval);
  } else {
    fprintf(stderr, "[exception]\n");
  }
}

/* This sample is meant to demonstrate a very simple Javascript program that
   calls our single C hook function.
*/
int main(int argc, const char *argv[]) {
  JSRuntime *jsrt;
  JSContext *jsctx;
  JSMemoryUsage stats;
  JSValue result;
  const char *script = "var x = Math.PI;\n"
                       "console.log('Hello');\n"
                       "console.log('x = ', x);\n"
                       "var y = Math.sin(x * 0.5);\n"
                       "console.log('SIN(x/2) = ', y);\n";

  jsrt = JS_NewRuntime();
  if (!jsrt) {
    fprintf(stderr, "qjs: cannot allocate JSRuntime\n");
    return 1;
  }
  jsctx = JS_NewContextRaw(jsrt);
  if (!jsctx) {
    fprintf(stderr, "qjs: cannot allocate JSContext\n");
    JS_FreeRuntime(jsrt);
    return 1;
  }

  JS_AddIntrinsicBaseObjects(jsctx);
  JS_AddIntrinsicDate(jsctx);
  JS_AddIntrinsicEval(jsctx);
  JS_AddIntrinsicRegExp(jsctx);
  JS_AddIntrinsicJSON(jsctx);
  JS_AddIntrinsicProxy(jsctx);
  JS_AddIntrinsicMapSet(jsctx);
  JS_AddIntrinsicTypedArrays(jsctx);
  JS_AddIntrinsicPromise(jsctx);
  JS_AddIntrinsicBigInt(jsctx);

  init_c_hooks(jsctx);

  result = JS_Eval(jsctx, script, strlen(script), "<input>", 0);
  if (JS_IsException(result)) {
    JSValue exception = JS_GetException(jsctx);
    int is_error = JS_IsError(jsctx, exception);
    dump_value_to_stream(jsctx, stderr, exception);
    if (is_error) {
      JSValue stack = JS_GetPropertyStr(jsctx, exception, "stack");
      if (!JS_IsUndefined(stack)) {
        dump_value_to_stream(jsctx, stderr, stack);
      }
      JS_FreeValue(jsctx, stack);
    }

    JS_FreeValue(jsctx, exception);
  }

  JS_FreeValue(jsctx, result);

  JS_ComputeMemoryUsage(jsrt, &stats);
  JS_DumpMemoryUsage(stdout, &stats, jsrt);

  JS_FreeContext(jsctx);
  JS_FreeRuntime(jsrt);

  return 0;
}
