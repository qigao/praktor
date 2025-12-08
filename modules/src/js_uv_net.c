#include "js_uv_internal.h"

#include <stdlib.h>
#include <string.h>
static int js_uv_net_ip_version_from_value(JSContext *ctx, JSValueConst value, int *out_version) {
  const char *addr = JS_ToCString(ctx, value);
  if (!addr) {
    return -1;
  }
  unsigned char buf[16];
  int version = 0;
  if (uv_inet_pton(AF_INET, addr, buf) == 0) {
    version = 4;
  } else if (uv_inet_pton(AF_INET6, addr, buf) == 0) {
    version = 6;
  }
  JS_FreeCString(ctx, addr);
  *out_version = version;
  return 0;
}

static JSValue js_uv_net_is_ip(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  (void)this_val;
  JSValueConst arg = argc > 0 ? argv[0] : JS_UNDEFINED;
  int version = 0;
  if (js_uv_net_ip_version_from_value(ctx, arg, &version) < 0) {
    return JS_EXCEPTION;
  }
  return JS_NewInt32(ctx, version);
}

static JSValue js_uv_net_is_ipv4(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv) {
  (void)this_val;
  JSValueConst arg = argc > 0 ? argv[0] : JS_UNDEFINED;
  int version = 0;
  if (js_uv_net_ip_version_from_value(ctx, arg, &version) < 0) {
    return JS_EXCEPTION;
  }
  return JS_NewBool(ctx, version == 4);
}

static JSValue js_uv_net_is_ipv6(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv) {
  (void)this_val;
  JSValueConst arg = argc > 0 ? argv[0] : JS_UNDEFINED;
  int version = 0;
  if (js_uv_net_ip_version_from_value(ctx, arg, &version) < 0) {
    return JS_EXCEPTION;
  }
  return JS_NewBool(ctx, version == 6);
}

static const JSCFunctionListEntry js_uv_net_funcs[] = {
    JS_CFUNC_DEF("isIP", 1, js_uv_net_is_ip),
    JS_CFUNC_DEF("isIPv4", 1, js_uv_net_is_ipv4),
    JS_CFUNC_DEF("isIPv6", 1, js_uv_net_is_ipv6),
};

int js_uv_register_net(JSContext *ctx, JSValue uv_obj) {
  JSValue net_obj = JS_NewObject(ctx);
  if (JS_IsException(net_obj)) {
    return -1;
  }
  JS_SetPropertyFunctionList(ctx, net_obj, js_uv_net_funcs, countof(js_uv_net_funcs));
  if (JS_SetPropertyStr(ctx, uv_obj, "net", net_obj) < 0) {
    JS_FreeValue(ctx, net_obj);
    return -1;
  }
  return 0;
}
