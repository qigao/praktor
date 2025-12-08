#include "js_uv_internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#ifdef _WIN32
  #include <direct.h>
#endif
typedef struct JSUVFsReq JSUVFsReq;
typedef enum {
  JS_UV_FS_STAT,
  JS_UV_FS_MKDIR,
  JS_UV_FS_UNLINK,
  JS_UV_FS_READDIR,
  JS_UV_FS_READFILE,
  JS_UV_FS_WRITEFILE,
} JSUVFsOperation;
struct JSUVFsReq {
  JSContext *ctx;
  JSUVPromise promise;
  JSUVFsOperation op;
  char *path;
  int flags;
  int mode;
  uv_fs_t req;
  int fd;
  uv_fs_t open_req;
  uv_fs_t read_req;
  uv_fs_t write_req;
  uv_fs_t close_req;
  uv_fs_t stat_req;
  JSUVByteBuffer data;
  uint8_t *scratch;
  size_t offset;
  bool binary;
  uint8_t *write_data;
  size_t write_length;
  size_t write_offset;
};
static void js_uv_fs_req_cleanup(JSUVFsReq *req) {
  if (!req) {
    return;
  }
  if (req->path) {
    free(req->path);
  }
  js_uv_buffer_free(&req->data);
  if (req->scratch) {
    free(req->scratch);
  }
  if (req->write_data) {
    free(req->write_data);
  }
  js_uv_promise_destroy(&req->promise);
  free(req);
}
static JSValue js_uv_stat_to_object(JSContext *ctx, const uv_stat_t *statbuf) {
  JSValue obj = JS_NewObject(ctx);
  if (JS_IsException(obj)) {
    return obj;
  }
#define SET_INT(name, value) JS_SetPropertyStr(ctx, obj, name, JS_NewInt64(ctx, (int64_t)(value)))
#define SET_BOOL(name, value) JS_SetPropertyStr(ctx, obj, name, JS_NewBool(ctx, (value)))
#define SET_FLOAT(name, value) JS_SetPropertyStr(ctx, obj, name, JS_NewFloat64(ctx, (value)))
  SET_INT("dev", statbuf->st_dev);
  SET_INT("mode", statbuf->st_mode);
  SET_INT("nlink", statbuf->st_nlink);
  SET_INT("uid", statbuf->st_uid);
  SET_INT("gid", statbuf->st_gid);
  SET_INT("rdev", statbuf->st_rdev);
  SET_INT("ino", statbuf->st_ino);
  SET_INT("size", statbuf->st_size);
  SET_INT("blksize", statbuf->st_blksize);
  SET_INT("blocks", statbuf->st_blocks);
  double atime = (double)statbuf->st_atim.tv_sec * 1000.0 + (double)statbuf->st_atim.tv_nsec / 1e6;
  double mtime = (double)statbuf->st_mtim.tv_sec * 1000.0 + (double)statbuf->st_mtim.tv_nsec / 1e6;
  double ctime = (double)statbuf->st_ctim.tv_sec * 1000.0 + (double)statbuf->st_ctim.tv_nsec / 1e6;
double birthtime = (double)statbuf->st_birthtim.tv_sec * 1000.0 + (double)statbuf->st_birthtim.tv_nsec / 1e6;
  SET_FLOAT("atimeMs", atime);
  SET_FLOAT("mtimeMs", mtime);
  SET_FLOAT("ctimeMs", ctime);
  SET_FLOAT("birthtimeMs", birthtime);
  SET_BOOL("isDirectory", S_ISDIR(statbuf->st_mode));
  SET_BOOL("isFile", S_ISREG(statbuf->st_mode));
  SET_BOOL("isSymbolicLink", S_ISLNK(statbuf->st_mode));
#undef SET_INT
#undef SET_BOOL
#undef SET_FLOAT
  return obj;
}
static void js_uv_fs_after_stat(uv_fs_t *req) {
  JSUVFsReq *op = (JSUVFsReq *)req->data;
  JSContext *ctx = op->ctx;
  if (req->result < 0) {
    js_uv_promise_reject_uv(&op->promise, (int)req->result, "uv_fs_stat");
  } else {
    JSValue stat_obj = js_uv_stat_to_object(ctx, &req->statbuf);
    if (JS_IsException(stat_obj)) {
      js_uv_dump_error(ctx);
      js_uv_promise_reject_message(&op->promise, "Failed to build stat result");
    } else {
      js_uv_promise_resolve(&op->promise, stat_obj);
    }
  }
  uv_fs_req_cleanup(req);
  js_uv_fs_req_cleanup(op);
}
static JSValue js_uv_fs_stat(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  (void)this_val;
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "path is required");
  }
  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path) {
    return JS_EXCEPTION;
  }
  JSUVFsReq *op = (JSUVFsReq *)calloc(1, sizeof(*op));
  if (!op) {
    JS_FreeCString(ctx, path);
    return JS_ThrowOutOfMemory(ctx);
  }
  op->ctx = ctx;
  js_uv_buffer_init(&op->data);
  JSValue promise;
  if (js_uv_promise_init(ctx, &op->promise, &promise) != 0) {
    JS_FreeCString(ctx, path);
    js_uv_fs_req_cleanup(op);
    return JS_EXCEPTION;
  }
  op->path = strdup(path);
  JS_FreeCString(ctx, path);
  if (!op->path) {
    JS_FreeValue(ctx, promise);
    js_uv_fs_req_cleanup(op);
    return JS_ThrowOutOfMemory(ctx);
  }
  op->req.data = op;
  int rc = uv_fs_stat(js_uv_loop(), &op->req, op->path, js_uv_fs_after_stat);
  if (rc < 0) {
    js_uv_promise_reject_uv(&op->promise, rc, "uv_fs_stat");
    uv_fs_req_cleanup(&op->req);
    JS_FreeValue(ctx, promise);
    js_uv_fs_req_cleanup(op);
    return JS_EXCEPTION;
  }
  return promise;
}
static void js_uv_fs_after_simple(uv_fs_t *req) {
  JSUVFsReq *op = (JSUVFsReq *)req->data;
  const char *syscall = op->op == JS_UV_FS_MKDIR ? "uv_fs_mkdir" : "uv_fs_unlink";
  if (req->result < 0) {
    js_uv_promise_reject_uv(&op->promise, (int)req->result, syscall);
  } else {
    js_uv_promise_resolve_undefined(&op->promise);
  }
  uv_fs_req_cleanup(req);
  js_uv_fs_req_cleanup(op);
}
static JSValue js_uv_fs_mkdir(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  (void)this_val;
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "path is required");
  }
  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path) {
    return JS_EXCEPTION;
  }
  int64_t mode = 0777;
  if (argc > 1 && JS_IsNumber(argv[1])) {
    if (JS_ToInt64(ctx, &mode, argv[1])) {
      JS_FreeCString(ctx, path);
      return JS_EXCEPTION;
    }
  }
  JSUVFsReq *op = (JSUVFsReq *)calloc(1, sizeof(*op));
  if (!op) {
    JS_FreeCString(ctx, path);
    return JS_ThrowOutOfMemory(ctx);
  }
  op->ctx = ctx;
  op->mode = (int)mode;
  op->op = JS_UV_FS_MKDIR;
  js_uv_buffer_init(&op->data);
  JSValue promise;
  if (js_uv_promise_init(ctx, &op->promise, &promise) != 0) {
    JS_FreeCString(ctx, path);
    js_uv_fs_req_cleanup(op);
    return JS_EXCEPTION;
  }
  op->path = strdup(path);
  JS_FreeCString(ctx, path);
  if (!op->path) {
    JS_FreeValue(ctx, promise);
    js_uv_fs_req_cleanup(op);
    return JS_ThrowOutOfMemory(ctx);
  }
  op->req.data = op;
  int rc = uv_fs_mkdir(js_uv_loop(), &op->req, op->path, op->mode, js_uv_fs_after_simple);
  if (rc < 0) {
    js_uv_promise_reject_uv(&op->promise, rc, "uv_fs_mkdir");
    uv_fs_req_cleanup(&op->req);
    JS_FreeValue(ctx, promise);
    js_uv_fs_req_cleanup(op);
    return JS_EXCEPTION;
  }
  return promise;
}
static JSValue js_uv_fs_unlink(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  (void)this_val;
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "path is required");
  }
  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path) {
    return JS_EXCEPTION;
  }
  JSUVFsReq *op = (JSUVFsReq *)calloc(1, sizeof(*op));
  if (!op) {
    JS_FreeCString(ctx, path);
    return JS_ThrowOutOfMemory(ctx);
  }
  op->ctx = ctx;
  op->op = JS_UV_FS_UNLINK;
  js_uv_buffer_init(&op->data);
  JSValue promise;
  if (js_uv_promise_init(ctx, &op->promise, &promise) != 0) {
    JS_FreeCString(ctx, path);
    js_uv_fs_req_cleanup(op);
    return JS_EXCEPTION;
  }
  op->path = strdup(path);
  JS_FreeCString(ctx, path);
  if (!op->path) {
    JS_FreeValue(ctx, promise);
    js_uv_fs_req_cleanup(op);
    return JS_ThrowOutOfMemory(ctx);
  }
  op->req.data = op;
  int rc = uv_fs_unlink(js_uv_loop(), &op->req, op->path, js_uv_fs_after_simple);
  if (rc < 0) {
    js_uv_promise_reject_uv(&op->promise, rc, "uv_fs_unlink");
    uv_fs_req_cleanup(&op->req);
    JS_FreeValue(ctx, promise);
    js_uv_fs_req_cleanup(op);
    return JS_EXCEPTION;
  }
  return promise;
}
static void js_uv_fs_after_scandir(uv_fs_t *req) {
  JSUVFsReq *op = (JSUVFsReq *)req->data;
  JSContext *ctx = op->ctx;
  if (req->result < 0) {
    js_uv_promise_reject_uv(&op->promise, (int)req->result, "uv_fs_scandir");
  } else {
    JSValue arr = JS_NewArray(ctx);
    if (JS_IsException(arr)) {
      js_uv_dump_error(ctx);
      js_uv_promise_reject_message(&op->promise, "Failed to allocate array");
    } else {
      uv_dirent_t dirent;
      uint32_t index = 0;
      bool rejected = false;
      int r;
      while ((r = uv_fs_scandir_next(req, &dirent)) != UV_EOF) {
        if (r < 0) {
          js_uv_promise_reject_uv(&op->promise, r, "uv_fs_scandir_next");
          rejected = true;
          break;
        }
        JSValue entry = JS_NewObject(ctx);
        if (JS_IsException(entry)) {
          js_uv_dump_error(ctx);
          JS_FreeValue(ctx, arr);
          arr = entry;
          break;
        }
        JS_SetPropertyStr(ctx, entry, "name", JS_NewString(ctx, dirent.name));
        JS_SetPropertyStr(ctx, entry, "type", JS_NewInt32(ctx, dirent.type));
        JS_DefinePropertyValueUint32(ctx, arr, index++, entry, JS_PROP_C_W_E);
      }
      if (rejected) {
        JS_FreeValue(ctx, arr);
      } else if (!JS_IsException(arr)) {
        js_uv_promise_resolve(&op->promise, arr);
      } else {
        js_uv_promise_reject_message(&op->promise, "Failed to build directory entries");
      }
    }
  }
  uv_fs_req_cleanup(req);
  js_uv_fs_req_cleanup(op);
}
static JSValue js_uv_fs_readdir(JSContext *ctx, JSValueConst this_val, int argc,
                                JSValueConst *argv) {
  (void)this_val;
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "path is required");
  }
  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path) {
    return JS_EXCEPTION;
  }
  JSUVFsReq *op = (JSUVFsReq *)calloc(1, sizeof(*op));
  if (!op) {
    JS_FreeCString(ctx, path);
    return JS_ThrowOutOfMemory(ctx);
  }
  op->ctx = ctx;
  op->op = JS_UV_FS_READDIR;
  js_uv_buffer_init(&op->data);
  JSValue promise;
  if (js_uv_promise_init(ctx, &op->promise, &promise) != 0) {
    JS_FreeCString(ctx, path);
    js_uv_fs_req_cleanup(op);
    return JS_EXCEPTION;
  }
  op->path = strdup(path);
  JS_FreeCString(ctx, path);
  if (!op->path) {
    JS_FreeValue(ctx, promise);
    js_uv_fs_req_cleanup(op);
    return JS_ThrowOutOfMemory(ctx);
  }
  op->req.data = op;
  int rc = uv_fs_scandir(js_uv_loop(), &op->req, op->path, 0, js_uv_fs_after_scandir);
  if (rc < 0) {
    js_uv_promise_reject_uv(&op->promise, rc, "uv_fs_scandir");
    uv_fs_req_cleanup(&op->req);
    JS_FreeValue(ctx, promise);
    js_uv_fs_req_cleanup(op);
    return JS_EXCEPTION;
  }
  return promise;
}
static void js_uv_fs_after_close_read(uv_fs_t *req) {
  JSUVFsReq *op = (JSUVFsReq *)req->data;
  if (req->result < 0) {
    js_uv_promise_reject_uv(&op->promise, (int)req->result, "uv_fs_close");
  } else {
    JSContext *ctx = op->ctx;
    JSValue result;
    if (op->binary) {
      result = JS_NewArrayBufferCopy(ctx, op->data.data, op->data.length);
    } else {
      result = JS_NewStringLen(ctx, (const char *)op->data.data, op->data.length);
    }
    if (JS_IsException(result)) {
      js_uv_dump_error(ctx);
      js_uv_promise_reject_message(&op->promise, "Failed to create readFile result");
    } else {
      js_uv_promise_resolve(&op->promise, result);
    }
  }
  uv_fs_req_cleanup(req);
  js_uv_fs_req_cleanup(op);
}
static void js_uv_fs_read_next(JSUVFsReq *op);
static void js_uv_fs_after_read(uv_fs_t *req) {
  JSUVFsReq *op = (JSUVFsReq *)req->data;
  if (req->result < 0) {
    int err = (int)req->result;
    uv_fs_req_cleanup(req);
    op->close_req.data = op;
    uv_fs_close(js_uv_loop(), &op->close_req, op->fd, js_uv_fs_after_close_read);
    js_uv_promise_reject_uv(&op->promise, err, "uv_fs_read");
    return;
  }
  ssize_t nread = req->result;
  uv_fs_req_cleanup(req);
  if (nread == 0) {
    op->close_req.data = op;
    uv_fs_close(js_uv_loop(), &op->close_req, op->fd, js_uv_fs_after_close_read);
    return;
  }
  if (js_uv_buffer_append(&op->data, (const uint8_t *)op->scratch, (size_t)nread) != 0) {
    js_uv_promise_reject_message(&op->promise, "out of memory");
    op->close_req.data = op;
    uv_fs_close(js_uv_loop(), &op->close_req, op->fd, js_uv_fs_after_close_read);
    return;
  }
  op->offset += (size_t)nread;
  js_uv_fs_read_next(op);
}
static void js_uv_fs_read_next(JSUVFsReq *op) {
  op->read_req.data = op;
  uv_buf_t buf = uv_buf_init((char *)op->scratch, JS_UV_READFILE_CHUNK);
  int rc = uv_fs_read(js_uv_loop(), &op->read_req, op->fd, &buf, 1, (ssize_t)op->offset,
                      js_uv_fs_after_read);
  if (rc < 0) {
    js_uv_promise_reject_uv(&op->promise, rc, "uv_fs_read");
    uv_fs_req_cleanup(&op->read_req);
    op->close_req.data = op;
    uv_fs_close(js_uv_loop(), &op->close_req, op->fd, js_uv_fs_after_close_read);
  }
}
static void js_uv_fs_after_fstat(uv_fs_t *req) {
  JSUVFsReq *op = (JSUVFsReq *)req->data;
  if (req->result < 0) {
    int err = (int)req->result;
    uv_fs_req_cleanup(req);
    js_uv_promise_reject_uv(&op->promise, err, "uv_fs_fstat");
    op->close_req.data = op;
    uv_fs_close(js_uv_loop(), &op->close_req, op->fd, js_uv_fs_after_close_read);
    return;
  }
  uv_fs_req_cleanup(req);
  js_uv_fs_read_next(op);
}
static void js_uv_fs_after_open_read(uv_fs_t *req) {
  JSUVFsReq *op = (JSUVFsReq *)req->data;
  if (req->result < 0) {
    js_uv_promise_reject_uv(&op->promise, (int)req->result, "uv_fs_open");
    uv_fs_req_cleanup(req);
    js_uv_fs_req_cleanup(op);
    return;
  }
  op->fd = (int)req->result;
  uv_fs_req_cleanup(req);
  op->stat_req.data = op;
  int rc = uv_fs_fstat(js_uv_loop(), &op->stat_req, op->fd, js_uv_fs_after_fstat);
  if (rc < 0) {
    js_uv_promise_reject_uv(&op->promise, rc, "uv_fs_fstat");
    uv_fs_req_cleanup(&op->stat_req);
    op->close_req.data = op;
    uv_fs_close(js_uv_loop(), &op->close_req, op->fd, js_uv_fs_after_close_read);
  }
}
static JSValue js_uv_fs_read_file(JSContext *ctx, JSValueConst this_val, int argc,
                                  JSValueConst *argv) {
  (void)this_val;
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "path is required");
  }
  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path) {
    return JS_EXCEPTION;
  }
  bool binary = false;
  if (argc > 1 && JS_IsString(argv[1])) {
    const char *mode = JS_ToCString(ctx, argv[1]);
    if (!mode) {
      JS_FreeCString(ctx, path);
      return JS_EXCEPTION;
    }
    if (strcmp(mode, "binary") == 0) {
      binary = true;
    }
    JS_FreeCString(ctx, mode);
  }
  JSUVFsReq *op = (JSUVFsReq *)calloc(1, sizeof(*op));
  if (!op) {
    JS_FreeCString(ctx, path);
    return JS_ThrowOutOfMemory(ctx);
  }
  op->ctx = ctx;
  op->op = JS_UV_FS_READFILE;
  op->binary = binary;
  js_uv_buffer_init(&op->data);
  op->scratch = (uint8_t *)malloc(JS_UV_READFILE_CHUNK);
  if (!op->scratch) {
    JS_FreeCString(ctx, path);
    js_uv_fs_req_cleanup(op);
    return JS_ThrowOutOfMemory(ctx);
  }
  JSValue promise;
  if (js_uv_promise_init(ctx, &op->promise, &promise) != 0) {
    JS_FreeCString(ctx, path);
    js_uv_fs_req_cleanup(op);
    return JS_EXCEPTION;
  }
  op->path = strdup(path);
  JS_FreeCString(ctx, path);
  if (!op->path) {
    JS_FreeValue(ctx, promise);
    js_uv_fs_req_cleanup(op);
    return JS_ThrowOutOfMemory(ctx);
  }
  op->open_req.data = op;
  int rc = uv_fs_open(js_uv_loop(), &op->open_req, op->path, O_RDONLY, 0, js_uv_fs_after_open_read);
  if (rc < 0) {
    js_uv_promise_reject_uv(&op->promise, rc, "uv_fs_open");
    uv_fs_req_cleanup(&op->open_req);
    JS_FreeValue(ctx, promise);
    js_uv_fs_req_cleanup(op);
    return JS_EXCEPTION;
  }
  return promise;
}
static void js_uv_fs_after_close_write(uv_fs_t *req) {
  JSUVFsReq *op = (JSUVFsReq *)req->data;
  if (req->result < 0) {
    js_uv_promise_reject_uv(&op->promise, (int)req->result, "uv_fs_close");
  } else {
    js_uv_promise_resolve_undefined(&op->promise);
  }
  uv_fs_req_cleanup(req);
  js_uv_fs_req_cleanup(op);
}
static void js_uv_fs_write_next(JSUVFsReq *op);
static void js_uv_fs_after_write(uv_fs_t *req) {
  JSUVFsReq *op = (JSUVFsReq *)req->data;
  if (req->result < 0) {
    int err = (int)req->result;
    uv_fs_req_cleanup(req);
    js_uv_promise_reject_uv(&op->promise, err, "uv_fs_write");
    op->close_req.data = op;
    uv_fs_close(js_uv_loop(), &op->close_req, op->fd, js_uv_fs_after_close_write);
    return;
  }
  ssize_t wrote = req->result;
  uv_fs_req_cleanup(req);
  op->write_offset += (size_t)wrote;
  if (op->write_offset >= op->write_length) {
    op->close_req.data = op;
    uv_fs_close(js_uv_loop(), &op->close_req, op->fd, js_uv_fs_after_close_write);
  } else {
    js_uv_fs_write_next(op);
  }
}
static void js_uv_fs_write_next(JSUVFsReq *op) {
  op->write_req.data = op;
  uv_buf_t buf = uv_buf_init((char *)op->write_data + op->write_offset,
                             (unsigned int)(op->write_length - op->write_offset));
  int rc = uv_fs_write(js_uv_loop(), &op->write_req, op->fd, &buf, 1, (ssize_t)op->write_offset,
                       js_uv_fs_after_write);
  if (rc < 0) {
    js_uv_promise_reject_uv(&op->promise, rc, "uv_fs_write");
    uv_fs_req_cleanup(&op->write_req);
    op->close_req.data = op;
    uv_fs_close(js_uv_loop(), &op->close_req, op->fd, js_uv_fs_after_close_write);
  }
}
static void js_uv_fs_after_open_write(uv_fs_t *req) {
  JSUVFsReq *op = (JSUVFsReq *)req->data;
  if (req->result < 0) {
    js_uv_promise_reject_uv(&op->promise, (int)req->result, "uv_fs_open");
    uv_fs_req_cleanup(req);
    js_uv_fs_req_cleanup(op);
    return;
  }
  op->fd = (int)req->result;
  uv_fs_req_cleanup(req);
  js_uv_fs_write_next(op);
}
static JSValue js_uv_fs_write_file(JSContext *ctx, JSValueConst this_val, int argc,
                                   JSValueConst *argv) {
  (void)this_val;
  if (argc < 2) {
    return JS_ThrowTypeError(ctx, "path and data are required");
  }
  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path) {
    return JS_EXCEPTION;
  }
  uint8_t *data_ptr = NULL;
  size_t data_len = 0;
  if (js_uv_collect_data(ctx, argv[1], &data_ptr, &data_len) != 0) {
    JS_FreeCString(ctx, path);
    return JS_EXCEPTION;
  }
  JSUVFsReq *op = (JSUVFsReq *)calloc(1, sizeof(*op));
  if (!op) {
    free(data_ptr);
    JS_FreeCString(ctx, path);
    return JS_ThrowOutOfMemory(ctx);
  }
  op->ctx = ctx;
  op->op = JS_UV_FS_WRITEFILE;
  op->write_data = data_ptr;
  op->write_length = data_len;
  js_uv_buffer_init(&op->data);
  JSValue promise;
  if (js_uv_promise_init(ctx, &op->promise, &promise) != 0) {
    JS_FreeCString(ctx, path);
    js_uv_fs_req_cleanup(op);
    return JS_EXCEPTION;
  }
  op->path = strdup(path);
  JS_FreeCString(ctx, path);
  if (!op->path) {
    JS_FreeValue(ctx, promise);
    js_uv_fs_req_cleanup(op);
    return JS_ThrowOutOfMemory(ctx);
  }
  op->open_req.data = op;
  int rc = uv_fs_open(js_uv_loop(), &op->open_req, op->path, O_WRONLY | O_CREAT | O_TRUNC, 0666,
                      js_uv_fs_after_open_write);
  if (rc < 0) {
    js_uv_promise_reject_uv(&op->promise, rc, "uv_fs_open");
    uv_fs_req_cleanup(&op->open_req);
    JS_FreeValue(ctx, promise);
    js_uv_fs_req_cleanup(op);
    return JS_EXCEPTION;
  }
  return promise;
}
static const JSCFunctionListEntry js_uv_fs_funcs[] = {
    JS_CFUNC_DEF("stat", 1, js_uv_fs_stat),
    JS_CFUNC_DEF("mkdir", 2, js_uv_fs_mkdir),
    JS_CFUNC_DEF("unlink", 1, js_uv_fs_unlink),
    JS_CFUNC_DEF("readdir", 1, js_uv_fs_readdir),
    JS_CFUNC_DEF("readFile", 2, js_uv_fs_read_file),
    JS_CFUNC_DEF("writeFile", 2, js_uv_fs_write_file),
};
int js_uv_register_fs(JSContext *ctx, JSValue uv_obj) {
  JSValue fs_obj = JS_NewObject(ctx);
  if (JS_IsException(fs_obj)) {
    return -1;
  }
  JS_SetPropertyFunctionList(ctx, fs_obj, js_uv_fs_funcs, countof(js_uv_fs_funcs));
  if (JS_SetPropertyStr(ctx, uv_obj, "fs", fs_obj) < 0) {
    JS_FreeValue(ctx, fs_obj);
    return -1;
  }
  return 0;
}
