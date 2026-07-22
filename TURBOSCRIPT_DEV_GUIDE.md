# TurboScript 模块扩展开发指南

> **受众**: 需要为 Praktor 脚本引擎新增内置函数或模块的 C++ 开发者
> **版本**: v9.1 | **最后更新**: 2026-06-17

---

## 目录

1. [架构概览](#1-架构概览)
2. [核心数据类型](#2-核心数据类型)
3. [ScriptEvalContext 结构](#3-scriptevalcontext-结构)
4. [函数签名规范](#4-函数签名规范)
5. [现有函数实现参考](#5-现有函数实现参考)
6. [注册新函数](#6-注册新函数)
7. [添加新模块（完整示例）](#7-添加新模块完整示例)
8. [WorkflowContext 访问接口](#8-workflowcontext-访问接口)
9. [错误处理约定](#9-错误处理约定)
10. [内存管理与生命周期](#10-内存管理与生命周期)
11. [测试策略](#11-测试策略)
12. [约束与禁止事项](#12-约束与禁止事项)

---

## 1. 架构概览

```
YAML script: 块
      │
      ▼
normalize_script_source()        ← 文本替换：ctx.get( → ctx_get(
      │
      ▼
turbo_script_init()              ← 创建 TurboScript JIT 上下文
      │
      ▼
ts_bind_func(ctx, "ctx_get", fn, &eval_ctx)   ← 注册所有 native functions
      │
      ▼
turbo_script_run_jit(ctx, source)             ← JIT 编译 + 执行
      │
      ▼
eval_ctx.failed / eval_ctx.failure_message    ← 检查脚本级失败
      │
      ▼
turbo_script_free(ctx)                        ← 清理
```

**关键约束**：TurboScript 运行时受全局互斥锁保护（`TurboScriptRuntimeGuard`），同一时刻只有一个脚本在执行。这是 TurboScript C 运行时的限制，不能并发执行多个脚本实例。

所有代码在：`praktor/src/script/script_engine.cpp`

---

## 2. 核心数据类型

TurboScript 的原生函数使用 `exprtk_value_t` 传递值，定义在外部 SDK 头文件中。

### 2.1 exprtk_value_t — 通用值类型

```c
// 类型标签
EXPRTK_VAL_NULL      // null
EXPRTK_VAL_NUMBER    // double
EXPRTK_VAL_STRING    // char* + len（非 null 终止）
EXPRTK_VAL_VECTOR    // double 数组
EXPRTK_VAL_LIST      // 任意类型列表
EXPRTK_VAL_MAP       // 字符串键 → 任意值的映射
EXPRTK_VAL_FUNCTION  // 函数引用（一般不使用）
```

### 2.2 构造辅助函数（script_engine.cpp 内已定义）

```cpp
// 返回 null
static exprtk_value_t make_null();

// 返回数值
static exprtk_value_t make_num(double val);

// 返回字符串（注意：data 指针必须保持有效！见 §10）
static exprtk_value_t make_str(const char* str, size_t len);
```

### 2.3 JSON ↔ exprtk_value_t 转换（script_engine.cpp 内已定义）

```cpp
// WorkflowValue → exprtk_value_t（深度转换）
static exprtk_value_t json_to_exprtk_value(const WorkflowValue& value,
                                            ScriptEvalContext& eval_ctx);

// exprtk_value_t → WorkflowValue（深度转换）
static WorkflowValue exprtk_scalar_to_json(const exprtk_value_t& value);
```

这两个函数处理所有类型的嵌套转换，包括对象、数组、数值、字符串和 null。

---

## 3. ScriptEvalContext 结构

`ScriptEvalContext` 是所有 native function 通过 `user_data` 指针接收的核心上下文：

```cpp
struct ScriptEvalContext {
  WorkflowContext& workflow_context;  // 工作流上下文（读写变量、任务输出）
  std::deque<std::string> string_pool;  // 延长字符串生命周期
  std::deque<std::vector<exprtk_value_t>> list_pool;  // 延长列表生命周期

  bool failed = false;                 // 脚本主动失败标志
  std::string failure_message;         // 失败原因

  // 将字符串放入池中并返回指向池内数据的 exprtk_value_t
  exprtk_value_t keep(std::string s);

  // 将列表放入池中并返回指向池内数据的 exprtk_value_t
  exprtk_value_t keepList(std::vector<exprtk_value_t> items);
};
```

**重要**：所有返回字符串的 native function 都必须通过 `eval_ctx->keep(s)` 延长生命周期，不能返回指向局部变量的指针。

---

## 4. 函数签名规范

所有 native function 必须符合以下签名：

```cpp
static exprtk_value_t my_fn(size_t argc, exprtk_value_t* args, void* user_data);
```

| 参数 | 说明 |
|------|------|
| `argc` | 调用时传入的参数个数 |
| `args` | 参数数组，`args[0]` 到 `args[argc-1]` |
| `user_data` | 注册时传入的指针，通常是 `&eval_ctx` 或 `nullptr` |

**防御性检查模式**（参考现有实现）：

```cpp
static exprtk_value_t my_fn(size_t argc, exprtk_value_t* args, void* user_data) {
  // 1. 检查最少参数数量
  if (argc < 1 || !user_data) {
    return make_null();
  }
  // 2. 检查参数类型
  if (args[0].type != EXPRTK_VAL_STRING) {
    return make_null();
  }
  // 3. 转换 user_data
  auto* eval_ctx = static_cast<ScriptEvalContext*>(user_data);

  // 4. 主体逻辑
  std::string input(args[0].data.string.data, args[0].data.string.len);
  // ...

  // 5. 返回结果
  return eval_ctx->keep(result_string);
}
```

**读取各类型参数**：

```cpp
// 读取字符串
std::string s(args[i].data.string.data, args[i].data.string.len);

// 读取数值
double n = args[i].data.number;

// 检查是否为 null
bool is_null = (args[i].type == EXPRTK_VAL_NULL);

// 读取 MAP 类型（对象）
if (args[i].type == EXPRTK_VAL_MAP) {
  WorkflowValue obj = exprtk_scalar_to_json(args[i]);
  // 通过 WorkflowValue 访问 obj["key"]
}
```

---

## 5. 现有函数实现参考

### 5.1 最简实现 — trim_fn_call

无副作用，只做字符串操作，返回字符串：

```cpp
static exprtk_value_t trim_fn_call(size_t argc, exprtk_value_t* args, void* user_data) {
  if (argc < 1 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
    return make_null();
  }
  auto* eval_ctx = static_cast<ScriptEvalContext*>(user_data);
  return eval_ctx->keep(
      trim_copy(std::string_view(args[0].data.string.data, args[0].data.string.len)));
}
```

### 5.2 读取上下文 — ctx_get_fn

通过 `workflow_context` 访问运行时状态，返回复杂类型：

```cpp
static exprtk_value_t ctx_get_fn(size_t argc, exprtk_value_t* args, void* user_data) {
  if (argc != 1 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
    return make_null();
  }
  auto* eval_ctx = static_cast<ScriptEvalContext*>(user_data);
  std::string path(args[0].data.string.data, args[0].data.string.len);
  auto val = eval_ctx->workflow_context.getValueByPath(path);
  return json_to_exprtk_value(val, *eval_ctx);  // 递归转换，池由 eval_ctx 管理
}
```

### 5.3 写入上下文 — ctx_output_fn

修改运行时状态，需要校验操作合法性：

```cpp
static exprtk_value_t ctx_output_fn(size_t argc, exprtk_value_t* args, void* user_data) {
  if (argc < 2 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
    return make_null();
  }
  auto* eval_ctx = static_cast<ScriptEvalContext*>(user_data);
  std::string key(args[0].data.string.data, args[0].data.string.len);
  WorkflowValue val = exprtk_scalar_to_json(args[1]);
  eval_ctx->workflow_context.setCurrentTaskOutput(key, val);
  return make_null();
}
```

### 5.4 主动失败 — fail_fn

设置 `eval_ctx->failed` 标志，脚本引擎在 `turbo_script_run_jit` 返回后检查此标志：

```cpp
static exprtk_value_t fail_fn(size_t argc, exprtk_value_t* args, void* user_data) {
  if (!user_data) return make_null();
  auto* eval_ctx = static_cast<ScriptEvalContext*>(user_data);
  eval_ctx->failed = true;
  if (argc >= 1 && args[0].type == EXPRTK_VAL_STRING) {
    eval_ctx->failure_message.assign(args[0].data.string.data, args[0].data.string.len);
  } else {
    eval_ctx->failure_message = "Script failed";
  }
  return make_null();
}
```

### 5.5 调用外部系统 — shell_exec_fn（可选参数 + 对象参数）

展示了如何处理可变数量参数、从 MAP 类型中读取选项：

```cpp
static exprtk_value_t shell_exec_fn(size_t argc, exprtk_value_t* args, void* user_data) {
  if (argc < 1 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
    return make_null();
  }
  auto* eval_ctx = static_cast<ScriptEvalContext*>(user_data);
  std::string command(args[0].data.string.data, args[0].data.string.len);

  // 第二个参数是可选的选项对象
  if (argc >= 2) {
    WorkflowValue options = build_shell_options(args[1]);
    // 从 options 读取各字段...
  }

  auto result = actions::ShellExecutor::execute(command, ...);

  // 返回 JSON 对象：序列化为字符串，通过 keep() 延长生命周期
  WorkflowValue payload = WorkflowValue::object();
  payload["exit_code"] = result.exit_code;
  payload["stdout"] = result.stdout_output;
  return eval_ctx->keep(payload.to_string());
}
```

---

## 6. 注册新函数

### 6.1 在 normalize_script_source 中添加别名

TurboScript 用点语法（`module.func()`）调用，但底层绑定必须是平坦标识符。在 `normalize_script_source()` 中添加文本替换规则：

```cpp
std::string normalize_script_source(std::string source) {
  // 已有规则
  replace_outside_strings(source, "ctx.get(",    "ctx_get(");
  // ...

  // 新增：为你的模块添加别名
  replace_outside_strings(source, "mymod.do_thing(",  "mymod_do_thing(");
  replace_outside_strings(source, "mymod.another(",   "mymod_another(");
  return source;
}
```

**注意**：`replace_outside_strings` 感知字符串字面量，不会替换字符串内部的文本。但不感知注释，避免在函数名中使用在代码注释里会频繁出现的词。

### 6.2 在 execute() 中绑定函数

在 `Praktor::Script::execute()` 函数的绑定区段加入：

```cpp
ScriptResult execute(const std::string& source, WorkflowContext& context) {
  // ...
  ScriptEvalContext eval_ctx{context, {}};

  // 已有绑定
  ts_bind_func(ctx, "ctx_get",  ctx_get_fn,  &eval_ctx);
  // ...

  // 新增绑定
  // 格式：ts_bind_func(turbo_ctx, "flat_name", fn_ptr, user_data)
  ts_bind_func(ctx, "mymod_do_thing",  mymod_do_thing_fn,  &eval_ctx);
  ts_bind_func(ctx, "mymod_another",   mymod_another_fn,   &eval_ctx);
  // ...
}
```

`user_data` 规则：
- 需要访问 `WorkflowContext` → 传 `&eval_ctx`
- 只做纯计算，不需要上下文 → 传 `nullptr`

---

## 7. 添加新模块（完整示例）

下面以添加一个 `crypto` 模块为例，实现 `crypto.md5(str)` 和 `crypto.sha256(str)` 两个函数。

### 步骤 1：实现 native function

在 `script_engine.cpp` 中添加（在现有函数之后，`execute()` 之前）：

```cpp
// ============================================================
// crypto 模块 — 哈希计算
// ============================================================

#include <openssl/md5.h>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>

// 工具函数：bytes → 十六进制字符串
static std::string bytes_to_hex(const unsigned char* data, size_t len) {
  std::ostringstream oss;
  for (size_t i = 0; i < len; ++i) {
    oss << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(data[i]);
  }
  return oss.str();
}

// crypto.md5(input_string) → 十六进制 MD5 字符串
static exprtk_value_t crypto_md5_fn(size_t argc, exprtk_value_t* args, void* user_data) {
  if (argc < 1 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
    return make_null();
  }
  auto* eval_ctx = static_cast<ScriptEvalContext*>(user_data);

  const std::string input(args[0].data.string.data, args[0].data.string.len);

  unsigned char digest[MD5_DIGEST_LENGTH];
  MD5(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);

  return eval_ctx->keep(bytes_to_hex(digest, MD5_DIGEST_LENGTH));
}

// crypto.sha256(input_string) → 十六进制 SHA-256 字符串
static exprtk_value_t crypto_sha256_fn(size_t argc, exprtk_value_t* args, void* user_data) {
  if (argc < 1 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
    return make_null();
  }
  auto* eval_ctx = static_cast<ScriptEvalContext*>(user_data);

  const std::string input(args[0].data.string.data, args[0].data.string.len);

  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);

  return eval_ctx->keep(bytes_to_hex(digest, SHA256_DIGEST_LENGTH));
}
```

### 步骤 2：添加文本替换规则

```cpp
std::string normalize_script_source(std::string source) {
  // ... 已有规则 ...
  replace_outside_strings(source, "crypto.md5(",    "crypto_md5(");
  replace_outside_strings(source, "crypto.sha256(", "crypto_sha256(");
  return source;
}
```

### 步骤 3：注册绑定

```cpp
ScriptResult execute(const std::string& source, WorkflowContext& context) {
  // ... 已有绑定 ...
  ts_bind_func(ctx, "crypto_md5",    crypto_md5_fn,    &eval_ctx);
  ts_bind_func(ctx, "crypto_sha256", crypto_sha256_fn, &eval_ctx);
  // ...
}
```

### 步骤 4：在 grammar.md 中更新文档

在 grammar.md § 4.3 中的内置模块表格添加 `crypto` 行，并新增模块说明节：

```markdown
**`crypto` — 哈希计算:**

- `crypto.md5(str)`: 返回输入字符串的 MD5 十六进制摘要（32 字符）。
- `crypto.sha256(str)`: 返回输入字符串的 SHA-256 十六进制摘要（64 字符）。
```

### 步骤 5：在 YAML 中使用

```yaml
- name: verify_integrity
  script: |
    var content = fs.read("./dist/release.tar.gz")
    var hash = crypto.sha256(content)
    var expected = ctx.get("variables.EXPECTED_HASH")
    if (hash != expected) {
        fail("Integrity check failed. Expected: " + expected + ", got: " + hash)
    }
    ctx.output("checksum", hash)
    log.info("Integrity verified: " + hash)
```

---

## 8. WorkflowContext 访问接口

`ScriptEvalContext::workflow_context` 是 `WorkflowContext&` 类型，以下是脚本函数最常用的接口：

### 8.1 读取值

```cpp
// 按路径读取（支持 tasks.NAME.outputs.KEY、variables.X、env.Y 等）
WorkflowValue val = eval_ctx->workflow_context.getValueByPath("tasks.build.outputs.stdout");

// 转换为 exprtk_value_t 返回给脚本
return json_to_exprtk_value(val, *eval_ctx);

// 直接读取字符串变量
std::string s = eval_ctx->workflow_context.getValueOrDefault<std::string>("variables.ENV", "");
```

### 8.2 写入输出

```cpp
// 写入当前任务的输出（推荐：通过 setCurrentTaskOutput）
WorkflowValue result_val = ...;
eval_ctx->workflow_context.setCurrentTaskOutput("result_key", result_val);

// 写入任意变量（禁止写保留路径，见 §12）
eval_ctx->workflow_context.setValue("variables.MY_VAR", some_json_value);
```

### 8.3 检查键是否存在

```cpp
bool exists = eval_ctx->workflow_context.hasKey("tasks.build.outputs.stdout");
```

### 8.4 WorkflowValue（Turbo Parser RAII 值）

`WorkflowValue` 拥有独立的 Turbo JSON DOM；复制执行深拷贝，查询结果不会借用已释放文档：

```cpp
WorkflowValue val = eval_ctx->workflow_context.getValueByPath(path);

if (val.is_null())   { /* 键不存在或为 null */ }
if (val.is_string()) { std::string s = val.as<std::string>(); }
if (val.is_number()) { double n = val.as<double>(); }
if (val.is_object()) { /* 遍历 val.object_range() */ }
if (val.is_array())  { /* 遍历 val.array_range()  */ }
```

---

## 9. 错误处理约定

### 9.1 三种失败模式

| 模式 | 使用场景 | 实现方式 |
|------|----------|----------|
| 返回 `make_null()` | 参数非法、可恢复的小错误 | `return make_null();` |
| 设置 failed 标志 | 业务逻辑失败（任务应标为失败） | `eval_ctx->failed = true;` |
| 抛出 C++ 异常 | 内部不可恢复错误 | 在 execute() 中捕获并转换为 ScriptResult::failed |

### 9.2 建议模式

```cpp
static exprtk_value_t my_fn(size_t argc, exprtk_value_t* args, void* user_data) {
  // 参数校验失败 → 返回 null（不让任务失败）
  if (argc < 1 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
    return make_null();
  }
  auto* eval_ctx = static_cast<ScriptEvalContext*>(user_data);

  try {
    // ... 主体逻辑 ...
    return eval_ctx->keep(result);
  } catch (const std::exception& e) {
    // 业务错误 → 设置 failed，让任务失败并显示错误信息
    eval_ctx->failed = true;
    eval_ctx->failure_message = std::string("my_fn error: ") + e.what();
    return make_null();
  }
}
```

### 9.3 日志

脚本引擎没有直接连接到 Praktor 的日志系统。如果需要在 native function 中输出日志，使用：

```cpp
#include "util/logging.hpp"
TLOG_WARN("my_fn: unexpected condition: {}", details);
```

---

## 10. 内存管理与生命周期

**核心规则**：`exprtk_value_t` 中的字符串指针（`data.string.data`）必须在函数返回后保持有效，直到 `turbo_script_run_jit` 返回。

### 10.1 正确：通过 eval_ctx->keep() 延长生命周期

```cpp
// 正确：字符串存入池中，池与 eval_ctx 同生命周期
std::string result = compute_something();
return eval_ctx->keep(std::move(result));  // keep() 接受 string，存入 deque
```

### 10.2 错误：返回指向局部变量的指针

```cpp
// ❌ 错误：局部变量在函数返回后销毁，悬垂指针
static exprtk_value_t bad_fn(...) {
  std::string result = "hello";
  return make_str(result.c_str(), result.size());  // result 在此之后析构！
}
```

### 10.3 返回 LIST 类型

```cpp
std::vector<exprtk_value_t> items;
items.push_back(eval_ctx->keep("item1"));
items.push_back(make_num(42.0));
return eval_ctx->keepList(std::move(items));  // 列表存入池中
```

### 10.4 返回 MAP（对象）类型

对于 MAP，通过 `json_to_exprtk_value` 转换 `WorkflowValue` 对象，其内部字符串会自动通过 `eval_ctx->keep()` 管理：

```cpp
WorkflowValue obj = WorkflowValue::object();
obj["key1"] = "value1";
obj["key2"] = 42;
return json_to_exprtk_value(obj, *eval_ctx);
```

---

## 11. 测试策略

### 11.1 单元测试新函数

在 `praktor/test/` 中找到或新建测试文件：

```cpp
// test_script_engine.cpp
#include "script/script_engine.hpp"
#include "dag/workflow_context.hpp"
#include <gtest/gtest.h>

TEST(ScriptEngine, CryptoMd5) {
  WorkflowContext ctx;
  auto result = Praktor::Script::execute(
      "ctx.output(\"hash\", crypto.md5(\"hello world\"))",
      ctx);

  ASSERT_TRUE(result.success) << result.error_message;

  // 获取任务输出
  // "hello world" 的 MD5 是 5eb63bbbe01eeed093cb22bb8f5acdc3
  auto hash = ctx.getValueByPath("tasks.__root__.outputs.hash");
  ASSERT_FALSE(hash.is_null());
  EXPECT_EQ(hash.as<std::string>(), "5eb63bbbe01eeed093cb22bb8f5acdc3");
}

TEST(ScriptEngine, CryptoMd5_EmptyInput) {
  WorkflowContext ctx;
  auto result = Praktor::Script::execute(
      "ctx.output(\"hash\", crypto.md5(\"\"))",
      ctx);
  ASSERT_TRUE(result.success);
}

TEST(ScriptEngine, CryptoMd5_InvalidArgs) {
  WorkflowContext ctx;
  // 不传参数，应返回 null，任务不失败
  auto result = Praktor::Script::execute(
      "var r = crypto.md5()\nctx.output(\"ok\", \"1\")",
      ctx);
  ASSERT_TRUE(result.success);  // 返回 null 不会失败
}
```

### 11.2 集成测试（YAML 层）

创建测试 YAML 文件：

```yaml
# test/scripts/test_crypto.yml
name: test-crypto-module
tasks:
  - name: verify_hash
    script: |
      var h = crypto.sha256("test input")
      if (h.length != 64) {
          fail("Expected 64-char SHA-256 hex, got length: " + h.length)
      }
      ctx.output("hash", h)
      log.info("SHA-256 hash: " + h)
```

在测试中运行：

```cpp
TEST(ScriptIntegration, CryptoModule) {
  // 通过 praktor CLI 运行或直接调用 WorkflowExecutor
  WorkflowContext ctx;
  auto result = Praktor::Script::execute(R"(
    var h = crypto.sha256("test input")
    if (h.length != 64) {
        fail("wrong length: " + h.length)
    }
    ctx.output("hash", h)
  )", ctx);

  ASSERT_TRUE(result.success) << result.error_message;
}
```

### 11.3 验证保留路径保护

如果你的函数接受写路径参数，确保测试保留路径被拒绝：

```cpp
TEST(ScriptEngine, NewModuleCannotWriteReservedPaths) {
  WorkflowContext ctx;
  // 假设 mymod.store(path, value) 写入上下文
  auto result = Praktor::Script::execute(
      "mymod.store(\"tasks.some_task.status\", \"hacked\")",
      ctx);
  // 应当失败或被静默忽略，取决于你的实现策略
  EXPECT_FALSE(result.success);
}
```

---

## 12. 约束与禁止事项

### 12.1 保留路径

以下上下文路径只能读，**禁止**从脚本写入：

| 路径前缀 | 原因 |
|----------|------|
| `tasks.*` | TaskRegistry 状态机管理，绕过会破坏不可变性约束 |
| `failure_context.*` | 由引擎在失败处理阶段注入，脚本不应修改 |
| `workflow_status` | 工作流全局状态，由引擎控制 |

在你的函数中如果接受写路径参数，必须加入同样的路径检查：

```cpp
// 参考 ctx_set_fn 的实现
if (path == "tasks" || path.rfind("tasks.", 0) == 0 ||
    path == "workflow_status" ||
    path == "failure_context" || path.rfind("failure_context.", 0) == 0) {
  eval_ctx->failed = true;
  eval_ctx->failure_message = "Cannot write reserved path: " + path;
  return make_null();
}
```

### 12.2 并发安全

- TurboScript 运行时由 `TurboScriptRuntimeGuard` 全局互斥锁保护，native function 在调用期间持有此锁
- **不要**在 native function 内部调用 `Praktor::Script::execute()`（会死锁）
- **不要**在 native function 内部创建新线程后访问 `eval_ctx`（`eval_ctx` 不是线程安全的）

### 12.3 异常安全

- native function 不得向外抛出 C++ 异常（TurboScript C 运行时不处理 C++ 异常）
- 所有 `try/catch` 必须在 native function 内部完整处理

### 12.4 normalize_script_source 的限制

`replace_outside_strings` 不感知 C 风格注释（`//`、`/* */`）。避免使用与常见注释词高度重合的函数名。如果函数名非常通用（如 `encode`），考虑加模块前缀（`base64_encode`）以减少意外替换的风险。

### 12.5 函数名命名约定

| 内部绑定名（flat） | 用户脚本调用语法 | 规范 |
|-------------------|-----------------|------|
| `ctx_get` | `ctx.get()` | 用下划线替代点 |
| `json_stringify` | `json.stringify()` | 模块名 + 下划线 + 方法名 |
| `crypto_sha256` | `crypto.sha256()` | 同上 |

不要使用 C++ 关键字或 TurboScript 内置标识符作为绑定名。

---

## 快速参考

### 添加新函数的 checklist

- [ ] 在 `script_engine.cpp` 中实现 `static exprtk_value_t my_fn(...)` 
- [ ] 返回字符串通过 `eval_ctx->keep(s)` 延长生命周期
- [ ] 业务失败通过 `eval_ctx->failed = true` 传播
- [ ] 异常在函数内部全部捕获
- [ ] 在 `normalize_script_source()` 中添加 `module.func(` → `module_func(` 的替换规则
- [ ] 在 `execute()` 的绑定区段调用 `ts_bind_func(ctx, "module_func", my_fn, user_data)`
- [ ] 在 `grammar.md` 的内置模块表格和说明中记录新函数
- [ ] 编写单元测试：正常路径、空输入、参数类型错误、异常路径
- [ ] 如果函数接受写路径参数，添加保留路径保护

### 关键文件索引

| 文件 | 作用 |
|------|------|
| `praktor/src/script/script_engine.cpp` | 所有 native function 实现和绑定 |
| `praktor/include/script/script_engine.hpp` | `ScriptResult`、`execute()` 声明 |
| `praktor/include/dag/workflow_context.hpp` | `WorkflowContext` 读写接口 |
| `praktor/include/dag/task_registry.hpp` | `TaskRegistry` 状态机 |
| `grammar.md` | DSL 规范，需同步更新 |
| `USER_GUIDE.md` | 用户文档，脚本模块说明需同步更新 |
