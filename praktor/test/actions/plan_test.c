#include "plan.h"
#include <tinytest.h>
#include <math.h>
#include <string.h>

static salts_bt_tree_config limits(void) {
  salts_bt_tree_config c; salts_bt_tree_config_init(&c);
  c.max_nodes = 16; c.max_depth = 8; c.max_control_iterations = 32; return c;
}
static praktor_c11_leaf_parameter number(const char *key, double value) {
  praktor_c11_leaf_parameter p = {key,PRAKTOR_C11_LEAF_NUMBER,NULL,value,false}; return p;
}
static praktor_c11_leaf_parameter text(const char *key, const char *value) {
  praktor_c11_leaf_parameter p = {key,PRAKTOR_C11_LEAF_STRING,value,0,false}; return p;
}
static int compile(const char *action, praktor_c11_leaf_parameter *p, size_t count,
                    size_t children, size_t parent, praktor_c11_session_node *out, bool *prune) {
  salts_bt_tree_config c = limits(); char error[256];
  return praktor_c11_compile_node(&c,action,p,count,NULL,children,parent,out,prune,error,sizeof(error));
}
static praktor_c11_session_node success(const char *action, praktor_c11_leaf_parameter *p,
                                        size_t count, size_t children, bool *prune) {
  praktor_c11_session_node n;
  check(compile(action,p,count,children,PRAKTOR_C11_BT_NO_PARENT,&n,prune) == SALTS_OK); return n;
}
spec("C11 control compiler migrated from C++ plan tests") {
  it("preserves parent-first descriptor relationships and borrowed typed input") {
    praktor_c11_session_node n; bool prune;
    n = success("Sequence",NULL,0,2,&prune);
    check(n.tree.kind == SALTS_BT_NODE_SEQUENCE && n.tree.parent_index == PRAKTOR_C11_BT_NO_PARENT && !prune);
    check(compile("WriteValue",NULL,0,0,0,&n,&prune) == SALTS_OK && n.tree.parent_index == 0 && n.tree.kind == SALTS_BT_NODE_LEAF);
    check(compile("CheckValue",NULL,0,0,0,&n,&prune) == SALTS_OK && n.tree.parent_index == 0);
  }
  it("preserves Retry counts and lowers zero attempts to FAILURE") {
    praktor_c11_leaf_parameter p = number("num_attempts",3); bool prune;
    praktor_c11_session_node n = success("Retry",&p,1,1,&prune);
    check(n.tree.kind == SALTS_BT_NODE_RETRY && n.tree.limit == 3 && !prune);
    p.number = 0; n = success("Retry",&p,1,1,&prune);
    check(prune && n.tree.kind == SALTS_BT_NODE_LEAF && n.terminal == PRAKTOR_C11_TERMINAL_FAILURE);
  }
  it("rejects invalid Retry attempts and validates arity before a zero result") {
    const double bad[] = {-1,1.5,33,NAN,INFINITY,2147483648.0}; size_t i;
    praktor_c11_session_node n; bool prune; praktor_c11_leaf_parameter p = number("num_attempts",0);
    for (i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) { p.number = bad[i]; check(compile("Retry",&p,1,1,0,&n,&prune) == SALTS_EINVAL); }
    p.number = 0; check(compile("Retry",&p,1,0,0,&n,&prune) == SALTS_EINVAL);
  }
  it("compiles Repeat counts and millisecond Delay parameters") {
    praktor_c11_leaf_parameter p = number("num_cycles",3); bool prune;
    praktor_c11_session_node n = success("Repeat",&p,1,1,&prune);
    check(n.tree.kind == SALTS_BT_NODE_REPEAT && n.tree.limit == 3 && !prune);
    p = number("delay_ms",12); n = success("Delay",&p,1,1,&prune);
    check(n.tree.kind == SALTS_BT_NODE_DELAY && n.tree.duration_ns == UINT64_C(12000000));
  }
  it("lowers empty Parallel and ReactiveSequence to SUCCESS") {
    const char *names[] = {"Parallel","ReactiveSequence"}; size_t i; bool prune;
    for (i = 0; i < 2; ++i) {
      praktor_c11_session_node n = success(names[i],NULL,0,0,&prune);
      check(prune && n.tree.kind == SALTS_BT_NODE_LEAF && n.terminal == PRAKTOR_C11_TERMINAL_SUCCESS && !n.tree.parallel_hooks);
    }
  }
  it("lowers empty ReactiveFallback to FAILURE") {
    bool prune; praktor_c11_session_node n = success("ReactiveFallback",NULL,0,0,&prune);
    check(prune && n.tree.kind == SALTS_BT_NODE_LEAF && n.terminal == PRAKTOR_C11_TERMINAL_FAILURE);
  }
  it("lowers a static zero-budget WhileDo to SUCCESS without evaluating its condition") {
    praktor_c11_leaf_parameter p[] = {number("max_iterations",0),text("condition","{missing}")}; bool prune;
    praktor_c11_session_node n = success("WhileDo",p,2,1,&prune);
    check(prune && n.tree.kind == SALTS_BT_NODE_LEAF && n.terminal == PRAKTOR_C11_TERMINAL_SUCCESS && !n.tree.condition_callback);
  }
  it("lowers a static zero-budget KeepRunningUntilFailure to FAILURE") {
    praktor_c11_leaf_parameter p = number("max_iterations",0); bool prune;
    praktor_c11_session_node n = success("KeepRunningUntilFailure",&p,1,1,&prune);
    check(prune && n.tree.kind == SALTS_BT_NODE_LEAF && n.terminal == PRAKTOR_C11_TERMINAL_FAILURE);
  }
  it("validates both WhileDo condition forms and zero-budget decorator arity") {
    praktor_c11_leaf_parameter p[] = {number("max_iterations",0),text("condition","true")}; bool prune;
    praktor_c11_session_node n;
    check(compile("WhileDo",p,2,0,0,&n,&prune) == SALTS_EINVAL);
    check(compile("WhileDo",p,1,1,0,&n,&prune) == SALTS_EINVAL);
    check(compile("KeepRunningUntilFailure",p,1,0,0,&n,&prune) == SALTS_EINVAL);
    n = success("WhileDo",p,1,2,&prune); check(prune && n.terminal == PRAKTOR_C11_TERMINAL_SUCCESS);
  }
  it("compiles runtime-bound Retry and Delay without consulting values") {
    praktor_c11_leaf_parameter p = text("num_attempts","{missing}"); bool prune;
    praktor_c11_session_node n = success("Retry",&p,1,1,&prune);
    check(!prune && n.tree.dynamic_parameter_mask == SALTS_BT_DYNAMIC_PARAMETER_LIMIT);
    p = text("delay_ms","{missing}"); n = success("Delay",&p,1,1,&prune);
    check(!prune && n.tree.dynamic_parameter_mask == SALTS_BT_DYNAMIC_PARAMETER_DURATION_NS);
  }
  it("keeps dynamic zero controls as runtime nodes") {
    praktor_c11_leaf_parameter p = text("num_attempts","{attempts}"); bool prune;
    praktor_c11_session_node n = success("Retry",&p,1,1,&prune);
    check(!prune && n.tree.kind == SALTS_BT_NODE_RETRY && n.tree.limit == 3 && n.terminal == PRAKTOR_C11_TERMINAL_RUNTIME);
  }
  it("accepts integral float text while leaving substituted Repeat and Delay values live") {
    praktor_c11_leaf_parameter p = text("num_cycles","3.0"); bool prune;
    praktor_c11_session_node n = success("Repeat",&p,1,1,&prune);
    check(n.tree.limit == 3);
    p.text = "{cycles}"; n = success("Repeat",&p,1,1,&prune);
    check(n.tree.limit == 0 && n.tree.dynamic_parameter_mask == SALTS_BT_DYNAMIC_PARAMETER_LIMIT);
    p = text("delay_ms","{delay}"); n = success("Delay",&p,1,1,&prune);
    check(n.tree.duration_ns == UINT64_C(100000000) && n.tree.dynamic_parameter_mask == SALTS_BT_DYNAMIC_PARAMETER_DURATION_NS);
  }
  it("rejects malformed controls and keeps empty Switch selector validation at runtime") {
    praktor_c11_leaf_parameter p = number("num_cycles",0); bool prune; praktor_c11_session_node n;
    const double bad[] = {-1,1.5,2147483648.0}; size_t i;
    for (i = 0; i < 3; ++i) { p.number = bad[i]; check(compile("Repeat",&p,1,1,0,&n,&prune) == SALTS_EINVAL); }
    p.number = 2; check(compile("Repeat",&p,1,0,0,&n,&prune) == SALTS_EINVAL);
    p = text("condition","true"); check(compile("Precondition",&p,1,2,0,&n,&prune) == SALTS_EINVAL);
    check(compile("Switch",NULL,0,0,0,&n,&prune) == SALTS_EINVAL);
    p = number("variable",0); n = success("Switch",&p,1,0,&prune);
    check(prune && n.terminal == PRAKTOR_C11_TERMINAL_SWITCH_FAILURE);
    check(compile("SubTree",NULL,0,0,0,&n,&prune) == SALTS_EINVAL);
    check(compile("UnknownControl",NULL,0,1,0,&n,&prune) == SALTS_EINVAL);
  }
}
