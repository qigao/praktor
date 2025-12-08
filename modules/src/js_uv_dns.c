#include "js_uv_internal.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <netinet/in.h>
#endif
typedef struct {
  JSContext *ctx;
  JSUVPromise promise;
  uv_getaddrinfo_t req;
  char *host;
  char *service;
  struct addrinfo hints;
} JSUVDnsReq;
static void js_uv_dns_after(uv_getaddrinfo_t *req, int status, struct addrinfo *res) {
  JSUVDnsReq *op = (JSUVDnsReq *)req->data;
  JSContext *ctx = op->ctx;
  if (status < 0) {
    js_uv_promise_reject_uv(&op->promise, status, "uv_getaddrinfo");
  } else {
    JSValue array = JS_NewArray(ctx);
    if (JS_IsException(array)) {
      js_uv_promise_reject_message(&op->promise, "Failed to allocate array");
    } else {
      uint32_t index = 0;
      char addr[INET6_ADDRSTRLEN];
      bool ok = true;
      for (struct addrinfo *item = res; item; item = item->ai_next) {
        JSValue entry = JS_NewObject(ctx);
        if (JS_IsException(entry)) {
          JS_FreeValue(ctx, array);
          js_uv_promise_reject_message(&op->promise, "Failed to allocate array entry");
          ok = false;
          break;
        }
        JS_SetPropertyStr(ctx, entry, "family", JS_NewInt32(ctx, item->ai_family));
        JS_SetPropertyStr(ctx, entry, "socktype", JS_NewInt32(ctx, item->ai_socktype));
        JS_SetPropertyStr(ctx, entry, "protocol", JS_NewInt32(ctx, item->ai_protocol));
        if (item->ai_canonname) {
          JS_SetPropertyStr(ctx, entry, "canonname", JS_NewString(ctx, item->ai_canonname));
        }
        if (item->ai_family == AF_INET) {
          struct sockaddr_in *addr4 = (struct sockaddr_in *)item->ai_addr;
          uv_ip4_name(addr4, addr, sizeof(addr));
          JS_SetPropertyStr(ctx, entry, "address", JS_NewString(ctx, addr));
          JS_SetPropertyStr(ctx, entry, "port", JS_NewInt32(ctx, ntohs(addr4->sin_port)));
        } else if (item->ai_family == AF_INET6) {
          struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)item->ai_addr;
          uv_ip6_name(addr6, addr, sizeof(addr));
          JS_SetPropertyStr(ctx, entry, "address", JS_NewString(ctx, addr));
          JS_SetPropertyStr(ctx, entry, "port", JS_NewInt32(ctx, ntohs(addr6->sin6_port)));
        }
        JS_DefinePropertyValueUint32(ctx, array, index++, entry, JS_PROP_C_W_E);
      }
      if (ok) {
        js_uv_promise_resolve(&op->promise, array);
      }
    }
  }
  if (res) {
    uv_freeaddrinfo(res);
  }
  free(op->host);
  free(op->service);
  js_uv_promise_destroy(&op->promise);
  free(op);
}
static JSValue js_uv_dns_getaddrinfo(JSContext *ctx, JSValueConst this_val, int argc,
                                     JSValueConst *argv) {
  (void)this_val;
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "host is required");
  }
  const char *node = JS_ToCString(ctx, argv[0]);
  if (!node) {
    return JS_EXCEPTION;
  }
  const char *service = NULL;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  if (argc > 1 && JS_IsObject(argv[1])) {
    JSValueConst options = argv[1];
    JSValue val = JS_GetPropertyStr(ctx, options, "port");
    if (!JS_IsUndefined(val)) {
      service = JS_ToCString(ctx, val);
      if (!service) {
        JS_FreeCString(ctx, node);
        JS_FreeValue(ctx, val);
        return JS_EXCEPTION;
      }
    }
    JS_FreeValue(ctx, val);
    val = JS_GetPropertyStr(ctx, options, "family");
    if (!JS_IsUndefined(val)) {
      int32_t family;
      if (JS_ToInt32(ctx, &family, val)) {
        JS_FreeCString(ctx, node);
        if (service)
          JS_FreeCString(ctx, service);
        JS_FreeValue(ctx, val);
        return JS_EXCEPTION;
      }
      hints.ai_family = family;
    }
    JS_FreeValue(ctx, val);
    val = JS_GetPropertyStr(ctx, options, "socktype");
    if (!JS_IsUndefined(val)) {
      int32_t socktype;
      if (JS_ToInt32(ctx, &socktype, val)) {
        JS_FreeCString(ctx, node);
        if (service)
          JS_FreeCString(ctx, service);
        JS_FreeValue(ctx, val);
        return JS_EXCEPTION;
      }
      hints.ai_socktype = socktype;
    }
    JS_FreeValue(ctx, val);
    val = JS_GetPropertyStr(ctx, options, "protocol");
    if (!JS_IsUndefined(val)) {
      int32_t protocol;
      if (JS_ToInt32(ctx, &protocol, val)) {
        JS_FreeCString(ctx, node);
        if (service)
          JS_FreeCString(ctx, service);
        JS_FreeValue(ctx, val);
        return JS_EXCEPTION;
      }
      hints.ai_protocol = protocol;
    }
    JS_FreeValue(ctx, val);
    val = JS_GetPropertyStr(ctx, options, "flags");
    if (!JS_IsUndefined(val)) {
      int32_t flags;
      if (JS_ToInt32(ctx, &flags, val)) {
        JS_FreeCString(ctx, node);
        if (service)
          JS_FreeCString(ctx, service);
        JS_FreeValue(ctx, val);
        return JS_EXCEPTION;
      }
      hints.ai_flags = flags;
    }
    JS_FreeValue(ctx, val);
  }
  JSUVDnsReq *op = (JSUVDnsReq *)calloc(1, sizeof(*op));
  if (!op) {
    JS_FreeCString(ctx, node);
    if (service)
      JS_FreeCString(ctx, service);
    return JS_ThrowOutOfMemory(ctx);
  }
  op->ctx = ctx;
  op->host = strdup(node);
  if (service) {
    op->service = strdup(service);
  }
  JS_FreeCString(ctx, node);
  if (service) {
    JS_FreeCString(ctx, service);
  }
  op->hints = hints;
  JSValue promise;
  if (js_uv_promise_init(ctx, &op->promise, &promise) != 0) {
    free(op->host);
    free(op->service);
    free(op);
    return JS_EXCEPTION;
  }
  op->req.data = op;
  int rc =
      uv_getaddrinfo(js_uv_loop(), &op->req, js_uv_dns_after, op->host, op->service, &op->hints);
  if (rc < 0) {
    js_uv_promise_reject_uv(&op->promise, rc, "uv_getaddrinfo");
    JS_FreeValue(ctx, promise);
    free(op->host);
    free(op->service);
    free(op);
    return JS_EXCEPTION;
  }
  return promise;
}
static const JSCFunctionListEntry js_uv_dns_funcs[] = {
    JS_CFUNC_DEF("getAddrInfo", 2, js_uv_dns_getaddrinfo),
};
int js_uv_register_dns(JSContext *ctx, JSValue uv_obj) {
  JSValue dns_obj = JS_NewObject(ctx);
  if (JS_IsException(dns_obj)) {
    return -1;
  }
  JS_SetPropertyFunctionList(ctx, dns_obj, js_uv_dns_funcs, countof(js_uv_dns_funcs));
  if (JS_SetPropertyStr(ctx, uv_obj, "dns", dns_obj) < 0) {
    JS_FreeValue(ctx, dns_obj);
    return -1;
  }
  return 0;
}
