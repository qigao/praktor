#ifndef __SCRIPT_EVALUATOR_HPP__
#define __SCRIPT_EVALUATOR_HPP__

#include "dag/workflow_context.hpp"
#include <quickjs.h>
#include <string>
#include <functional>
#include <sstream>

namespace Praktor::Execution {

class ScriptEvaluator {
public:
    ScriptEvaluator(WorkflowContext& context, const std::string& task_name);
    ~ScriptEvaluator();

    void registerGlobalFunction(const std::string& name, JSCFunction* func, int length);
    void registerGlobalObject(const std::string& name, JSValue obj);

    bool execute(const std::string& script, std::string& output, std::string& error);

    // Callbacks to expose context
    static JSValue js_print(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
    static JSValue js_fail(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
    static JSValue js_context_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
    static JSValue js_context_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
    static JSValue js_context_has(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

    JSContext* ctx() { return ctx_; }

private:
    JSRuntime* rt_;
    JSContext* ctx_;
    WorkflowContext& context_;
    std::string task_name_;
    std::ostringstream output_stream_;

    struct Opaque {
        ScriptEvaluator* evaluator;
        WorkflowContext* context;
    } opaque_;
};

} // namespace Praktor::Execution

#endif // __SCRIPT_EVALUATOR_HPP__
