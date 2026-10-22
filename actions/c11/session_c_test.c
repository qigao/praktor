#include "session.h"
#include "native_host.h"
#include <salts/clock.h>
#include <salts/thread.h>
#include <tinytest.h>
#include <stdlib.h>
#include <string.h>

typedef struct fixture {
  praktor_c11_native_host native;
  praktor_c11_bt_host native_hooks;
  praktor_c11_session_config config;
  praktor_c11_session session;
  uint64_t now;
  size_t calls, halts;
  bool decline, fail, running, reenter;
} fixture;
static uint64_t now_ns(void *user) { return ((fixture *)user)->now; }
static praktor_c11_value_store *values(fixture *f) { return praktor_c11_native_host_values(&f->native); }
static void set(fixture *f, const char *key, const char *value) {
  check(praktor_c11_value_store_set(values(f),key,value) == PRAKTOR_C11_VALUE_OK);
}
static void expect(fixture *f, const char *key, const char *expected) {
  char text[4097]; size_t size;
  check(praktor_c11_value_store_get(values(f),key,text,sizeof(text),&size) == PRAKTOR_C11_VALUE_OK);
  check(strcmp(text,expected) == 0);
}
static int tick(void *user, const praktor_c11_leaf_request *req, salts_bt_node_status *status) {
  fixture *f = user;
  ++f->calls;
  if (f->reenter) {
    salts_bt_node_status ignored;
    check(praktor_c11_session_tick(&f->session,&ignored) == SALTS_EBUSY);
    check(praktor_c11_session_reset(&f->session) == SALTS_EBUSY);
    check(praktor_c11_session_cancel(&f->session) == SALTS_EBUSY);
    check(praktor_c11_session_destroy(&f->session) == SALTS_EBUSY);
  }
  if (f->decline) return SALTS_ENOTSUP;
  if (f->fail) return SALTS_EIO;
  if (!strcmp(req->action,"Stage")) { expect(f,"pipeline_input",f->calls == 1 ? "seed" : "next"); set(f,"pipeline_output","next"); *status = SALTS_BT_SUCCESS; return SALTS_OK; }
  if (!strcmp(req->action,"Observe")) { *status = f->running ? SALTS_BT_RUNNING : SALTS_BT_SUCCESS; return SALTS_OK; }
  if (!strcmp(req->action,"Fail")) { *status = SALTS_BT_FAILURE; return SALTS_OK; }
  return f->native_hooks.tick_typed_leaf(f->native_hooks.user,req,status);
}
static void halt(void *user, const praktor_c11_leaf_request *req) {
  fixture *f = user; ++f->halts;
  f->native_hooks.halt_typed_leaf(f->native_hooks.user,req);
}
/* A C value provider can intentionally support an empty key even though the
 * bounded Native Host rejects empty storage keys. Exercise the Session's
 * numeric-control substitution contract independently of that policy. */
static int empty_get(void *user, const char *key, char *out, size_t cap, size_t *size) {
  fixture *f = user;
  return f->native_hooks.value_get(f->native_hooks.value_user,*key ? key : "empty_control",out,cap,size);
}
static int empty_set(void *user, const char *key, const char *value) {
  fixture *f = user;
  return f->native_hooks.value_set(f->native_hooks.value_user,*key ? key : "empty_control",value);
}
static int empty_contains(void *user, const char *key, int *contains) {
  fixture *f = user;
  return f->native_hooks.value_contains(f->native_hooks.value_user,*key ? key : "empty_control",contains);
}
static void init(fixture *f) {
  praktor_c11_native_host_config host = {32,128,4096,2,now_ns,f};
  memset(f,0,sizeof(*f));
  check(praktor_c11_native_host_init_with_config(&f->native,&host) == SALTS_OK);
  check(praktor_c11_native_host_bind(&f->native,&f->native_hooks) == SALTS_OK);
  praktor_c11_session_config_init(&f->config);
  f->config.host = f->native_hooks;
  f->config.host.user = f; f->config.host.tick_typed_leaf = tick; f->config.host.halt_typed_leaf = halt;
}
static void destroy(fixture *f) {
  check(praktor_c11_session_destroy(&f->session) == SALTS_OK && !f->session.impl);
  praktor_c11_native_host_destroy(&f->native);
}
static praktor_c11_session_node node(const char *action, salts_bt_node_kind kind, size_t parent) {
  praktor_c11_session_node n = {0}; n.action = action; n.tree.kind = kind; n.tree.parent_index = parent; return n;
}
static praktor_c11_leaf_parameter text(const char *name, const char *value) {
  praktor_c11_leaf_parameter p = {name,PRAKTOR_C11_LEAF_STRING,value,0,false}; return p;
}
static salts_bt_node_status drive(fixture *f) {
  salts_bt_node_status status = SALTS_BT_RUNNING;
  check(praktor_c11_session_tick(&f->session,&status) == SALTS_OK); return status;
}
static void drain(cflow_process_retainer *retainer) {
  size_t pending = 1, reaped = 0;
  uint64_t started = cmeta_hrtime();
  while (pending && cmeta_hrtime()-started < UINT64_C(6000000000)) {
    int r = cflow_process_retainer_run_ready(retainer,64,&reaped,&pending);
    check(r == SALTS_OK || r == SALTS_EBUSY);
    if (pending) cmeta_sleep_ms(1);
  }
  check(!pending); check(cflow_process_retainer_destroy(retainer) == SALTS_OK);
}

spec("C11 Session without a C++ compiler or runtime") {
  it("owns a cloned typed plan after the caller changes construction storage") {
    fixture f; char action[] = "SetVariable", value[] = "original";
    praktor_c11_leaf_parameter p[] = {text("key","result"),text("value",value)};
    praktor_c11_session_node n = node(action,SALTS_BT_NODE_LEAF,PRAKTOR_C11_BT_NO_PARENT);
    init(&f); n.parameters = p; n.parameter_count = 2;
    check(praktor_c11_session_init(&f.session,&f.config,&n,1) == SALTS_OK);
    memset(action,'x',sizeof(action)-1); memset(value,'y',sizeof(value)-1); memset(p,0,sizeof(p));
    check(praktor_c11_session_validate(&f.session) == SALTS_OK);
    check(drive(&f) == SALTS_BT_SUCCESS); expect(&f,"result","original"); destroy(&f);
  }
  it("rejects invalid topology, incomplete contracts and oversized plans before callbacks") {
    fixture f; praktor_c11_session_node n = node("Observe",SALTS_BT_NODE_LEAF,0);
    init(&f);
    check(praktor_c11_session_init(&f.session,&f.config,&n,1) != SALTS_OK && !f.session.impl);
    n.tree.parent_index = PRAKTOR_C11_BT_NO_PARENT; f.config.max_plan_bytes = 1;
    check(praktor_c11_session_init(&f.session,&f.config,&n,1) == SALTS_ENOBUFS && !f.session.impl);
    f.config.max_plan_bytes = 4096; f.config.host.halt_typed_leaf = NULL;
    check(praktor_c11_session_init(&f.session,&f.config,&n,1) == SALTS_EINVAL && !f.session.impl);
    check(!f.calls); destroy(&f);
  }
  it("resolves live Switch values and refuses malformed selectors before child effects") {
    fixture f; praktor_c11_leaf_parameter p = text("variable","{choice}");
    praktor_c11_session_node ns[] = {node("Switch",SALTS_BT_NODE_SWITCH,PRAKTOR_C11_BT_NO_PARENT),node("Fail",SALTS_BT_NODE_LEAF,0),node("Observe",SALTS_BT_NODE_LEAF,0)};
    salts_bt_node_status status; init(&f); ns[0].parameters = &p; ns[0].parameter_count = 1;
    check(praktor_c11_session_init(&f.session,&f.config,ns,3) == SALTS_OK);
    set(&f,"choice","1.0"); check(drive(&f) == SALTS_BT_SUCCESS);
    check(praktor_c11_session_reset(&f.session) == SALTS_OK); set(&f,"choice","0"); check(drive(&f) == SALTS_BT_FAILURE);
    check(praktor_c11_session_reset(&f.session) == SALTS_OK); set(&f,"choice","1.25");
    check(praktor_c11_session_tick(&f.session,&status) == SALTS_EINVAL); check(f.calls == 2); destroy(&f);
  }
  it("revalidates dynamic retry limits and preserves the iteration budget") {
    fixture f; praktor_c11_leaf_parameter p = text("num_attempts","{attempts}");
    praktor_c11_session_node ns[] = {node("Retry",SALTS_BT_NODE_RETRY,PRAKTOR_C11_BT_NO_PARENT),node("Fail",SALTS_BT_NODE_LEAF,0)};
    salts_bt_node_status status; init(&f); ns[0].tree.dynamic_parameter_mask = SALTS_BT_DYNAMIC_PARAMETER_LIMIT; ns[0].tree.limit = 3;
    ns[0].parameters = &p; ns[0].parameter_count = 1;
    check(praktor_c11_session_init(&f.session,&f.config,ns,2) == SALTS_OK);
    set(&f,"attempts","3"); check(drive(&f) == SALTS_BT_FAILURE); check(f.calls == 3);
    check(praktor_c11_session_reset(&f.session) == SALTS_OK); set(&f,"attempts","-1");
    check(praktor_c11_session_tick(&f.session,&status) == SALTS_EINVAL && f.calls == 3); destroy(&f);
  }
  it("preserves empty-key numeric substitutions without changing ordinary string ports") {
    fixture f; praktor_c11_leaf_parameter p = text("num_attempts","{}");
    praktor_c11_session_node ns[] = {node("Retry",SALTS_BT_NODE_RETRY,PRAKTOR_C11_BT_NO_PARENT),node("Fail",SALTS_BT_NODE_LEAF,0)};
    init(&f); set(&f,"empty_control","2");
    f.config.host.value_get = empty_get; f.config.host.value_set = empty_set;
    f.config.host.value_contains = empty_contains; f.config.host.value_user = &f;
    ns[0].parameters = &p; ns[0].parameter_count = 1; ns[0].tree.limit = 3;
    ns[0].tree.dynamic_parameter_mask = SALTS_BT_DYNAMIC_PARAMETER_LIMIT;
    check(praktor_c11_session_init(&f.session,&f.config,ns,2) == SALTS_OK);
    check(drive(&f) == SALTS_BT_FAILURE && f.calls == 2); destroy(&f);
    init(&f); { praktor_c11_leaf_parameter ports[] = {text("key","literal"),text("value","{}")};
      praktor_c11_session_node leaf = node("SetVariable",SALTS_BT_NODE_LEAF,PRAKTOR_C11_BT_NO_PARENT);
      leaf.parameters = ports; leaf.parameter_count = 2;
      check(praktor_c11_session_init(&f.session,&f.config,&leaf,1) == SALTS_OK);
      check(drive(&f) == SALTS_BT_SUCCESS); expect(&f,"literal","{}"); destroy(&f);
    }
  }
  it("restores pipeline input, publishes the final value and bounds retained state") {
    fixture f; praktor_c11_session_node ns[] = {node("PipelineSequence",SALTS_BT_NODE_PIPELINE_SEQUENCE,PRAKTOR_C11_BT_NO_PARENT),node("Stage",SALTS_BT_NODE_LEAF,0),node("Stage",SALTS_BT_NODE_LEAF,0)};
    salts_bt_node_status status; init(&f); set(&f,"pipeline_input","seed");
    check(praktor_c11_session_init(&f.session,&f.config,ns,3) == SALTS_OK);
    check(drive(&f) == SALTS_BT_SUCCESS); expect(&f,"pipeline_result","next"); destroy(&f);
    init(&f); set(&f,"pipeline_input","seed"); f.config.max_state_bytes = 2;
    check(praktor_c11_session_init(&f.session,&f.config,ns,3) == SALTS_OK);
    check(praktor_c11_session_tick(&f.session,&status) == SALTS_ENOBUFS && !f.calls); destroy(&f);
  }
  it("tracks EntryUpdated observations and clears them on cancellation") {
    fixture f; praktor_c11_leaf_parameter p = text("watch_key","watched");
    praktor_c11_session_node ns[] = {node("EntryUpdated",SALTS_BT_NODE_ENTRY_UPDATED,PRAKTOR_C11_BT_NO_PARENT),node("Observe",SALTS_BT_NODE_LEAF,0)};
    init(&f); ns[0].parameters = &p; ns[0].parameter_count = 1; set(&f,"watched","one");
    check(praktor_c11_session_init(&f.session,&f.config,ns,2) == SALTS_OK);
    check(drive(&f) == SALTS_BT_SUCCESS && f.calls == 1);
    check(drive(&f) == SALTS_BT_SUCCESS && f.calls == 1);
    set(&f,"watched","two"); check(drive(&f) == SALTS_BT_SUCCESS && f.calls == 2);
    check(praktor_c11_session_cancel(&f.session) == SALTS_OK); check(drive(&f) == SALTS_BT_SUCCESS && f.calls == 3); destroy(&f);
  }
  it("dequeues JSON before the child and retains that write after child failure") {
    fixture f; praktor_c11_session_node ns[] = {node("ConsumeQueue",SALTS_BT_NODE_CONSUME_QUEUE,PRAKTOR_C11_BT_NO_PARENT),node("Fail",SALTS_BT_NODE_LEAF,0)};
    init(&f); set(&f,"task_queue","[1,2]");
    check(praktor_c11_session_init(&f.session,&f.config,ns,2) == SALTS_OK);
    check(drive(&f) == SALTS_BT_FAILURE); expect(&f,"task_queue","[2]"); expect(&f,"current_item","1");
    check(praktor_c11_session_reset(&f.session) == SALTS_OK); set(&f,"task_queue","broken");
    check(drive(&f) == SALTS_BT_FAILURE && f.calls == 1); expect(&f,"task_queue","broken");
    check(strstr(praktor_c11_session_error(&f.session),"valid JSON") != NULL); destroy(&f);
  }
  it("pins a running Host owner and halts exactly once after a resumed decline") {
    fixture f; praktor_c11_session_node n = node("Observe",SALTS_BT_NODE_LEAF,PRAKTOR_C11_BT_NO_PARENT);
    salts_bt_node_status status; init(&f); f.running = true; f.reenter = true;
    check(praktor_c11_session_init(&f.session,&f.config,&n,1) == SALTS_OK);
    check(drive(&f) == SALTS_BT_RUNNING); f.decline = true;
    check(praktor_c11_session_tick(&f.session,&status) == SALTS_ENOTSUP && f.halts == 1);
    check(praktor_c11_session_cancel(&f.session) == SALTS_OK && f.halts == 1); destroy(&f);
  }
  it("halts a partially acquired Host activation on its first tick failure") {
    fixture f; praktor_c11_session_node n = node("Observe",SALTS_BT_NODE_LEAF,PRAKTOR_C11_BT_NO_PARENT);
    salts_bt_node_status status; init(&f); f.fail = true;
    check(praktor_c11_session_init(&f.session,&f.config,&n,1) == SALTS_OK);
    check(praktor_c11_session_tick(&f.session,&status) == SALTS_EIO && f.halts == 1); destroy(&f); check(f.halts == 1);
  }
  it("cancels and resets cooperative native Sleep without retaining C++ state") {
    fixture f; praktor_c11_leaf_parameter p = text("duration","1s");
    praktor_c11_session_node n = node("Sleep",SALTS_BT_NODE_LEAF,PRAKTOR_C11_BT_NO_PARENT);
    init(&f); n.parameters = &p; n.parameter_count = 1;
    check(praktor_c11_session_init(&f.session,&f.config,&n,1) == SALTS_OK);
    check(drive(&f) == SALTS_BT_RUNNING); f.now = UINT64_C(999999999); check(drive(&f) == SALTS_BT_RUNNING);
    check(praktor_c11_session_cancel(&f.session) == SALTS_OK && f.halts == 1);
    f.now = UINT64_C(1000000000); check(drive(&f) == SALTS_BT_RUNNING);
    f.now = UINT64_C(2000000000); check(drive(&f) == SALTS_BT_SUCCESS);
    check(praktor_c11_session_reset(&f.session) == SALTS_OK); check(drive(&f) == SALTS_BT_RUNNING); destroy(&f); check(f.halts == 2);
  }
  it("times out a running native leaf and rejects invalid dynamic deadlines") {
    fixture f; salts_bt_node_status status;
    praktor_c11_leaf_parameter timeout = text("timeout_ms","{deadline}"), sleep = text("duration","10s");
    praktor_c11_session_node ns[] = {node("Timeout",SALTS_BT_NODE_TIMEOUT,PRAKTOR_C11_BT_NO_PARENT),node("Sleep",SALTS_BT_NODE_LEAF,0)};
    init(&f); ns[0].parameters = &timeout; ns[0].parameter_count = 1;
    ns[0].tree.dynamic_parameter_mask = SALTS_BT_DYNAMIC_PARAMETER_DURATION_NS;
    ns[0].tree.duration_ns = UINT64_C(1000000); ns[1].parameters = &sleep; ns[1].parameter_count = 1;
    set(&f,"deadline","2147483647");
    check(praktor_c11_session_init(&f.session,&f.config,ns,2) == SALTS_OK);
    check(drive(&f) == SALTS_BT_RUNNING); set(&f,"deadline","0");
    check(drive(&f) == SALTS_BT_FAILURE && f.halts == 1);
    check(praktor_c11_session_reset(&f.session) == SALTS_OK); set(&f,"deadline","-1");
    check(praktor_c11_session_tick(&f.session,&status) == SALTS_EINVAL && f.calls == 1); destroy(&f);
  }
  it("evaluates substituted conditions before guarded child effects") {
    fixture f; praktor_c11_leaf_parameter p = text("condition","{guard}");
    praktor_c11_session_node ns[] = {node("Precondition",SALTS_BT_NODE_PRECONDITION,PRAKTOR_C11_BT_NO_PARENT),node("Observe",SALTS_BT_NODE_LEAF,0)};
    init(&f); ns[0].tree.condition_callback = true; ns[0].parameters = &p; ns[0].parameter_count = 1;
    check(praktor_c11_session_init(&f.session,&f.config,ns,2) == SALTS_OK);
    set(&f,"guard","OFF"); check(drive(&f) == SALTS_BT_FAILURE && !f.calls);
    set(&f,"guard","yes"); check(drive(&f) == SALTS_BT_SUCCESS && f.calls == 1); destroy(&f);
  }
  it("finishes each successful JSON item and consumes a complete queue") {
    fixture f; praktor_c11_session_node ns[] = {node("ConsumeQueue",SALTS_BT_NODE_CONSUME_QUEUE,PRAKTOR_C11_BT_NO_PARENT),node("Observe",SALTS_BT_NODE_LEAF,0)};
    init(&f); set(&f,"task_queue","[1,2,3]");
    check(praktor_c11_session_init(&f.session,&f.config,ns,2) == SALTS_OK);
    { salts_bt_node_status status = SALTS_BT_RUNNING; size_t attempts = 0;
      while (status == SALTS_BT_RUNNING && attempts++ < 8) status = drive(&f);
      check(status == SALTS_BT_SUCCESS && f.calls == 3);
    }
    expect(&f,"task_queue","[]"); expect(&f,"current_item","3"); destroy(&f);
  }
  it("executes Shell using only the C Session, native Host and Salts Process") {
    fixture f; cflow_process_retainer retainer = {0};
    praktor_c11_leaf_parameter p[] = {text("cmd","echo c11-session"),text("stream_output","false")};
    praktor_c11_session_node n = node("Shell",SALTS_BT_NODE_LEAF,PRAKTOR_C11_BT_NO_PARENT);
    salts_bt_node_status status = SALTS_BT_RUNNING; uint64_t started;
    init(&f); check(cflow_process_retainer_init(&retainer,1) == SALTS_OK); f.config.retainer = &retainer;
    n.parameters = p; n.parameter_count = 2;
    check(praktor_c11_session_init(&f.session,&f.config,&n,1) == SALTS_OK);
    started = cmeta_hrtime();
    while (status == SALTS_BT_RUNNING && cmeta_hrtime()-started < UINT64_C(6000000000)) { status = drive(&f); if (status == SALTS_BT_RUNNING) cmeta_sleep_ms(1); }
    check(status == SALTS_BT_SUCCESS); expect(&f,"shell_exit_code","0"); destroy(&f); drain(&retainer);
  }
  it("detaches destroyed Host state before a zero-budget Process retainer transfer") {
    fixture *f = calloc(1,sizeof(*f)); cflow_process_retainer retainer = {0};
#if defined(_WIN32)
    praktor_c11_leaf_parameter p = text("cmd","ping -n 6 127.0.0.1 > NUL");
#else
    praktor_c11_leaf_parameter p = text("cmd","sleep 5");
#endif
    praktor_c11_session_node n = node("Shell",SALTS_BT_NODE_LEAF,PRAKTOR_C11_BT_NO_PARENT);
    int r;
    check(f != NULL); init(f); check(cflow_process_retainer_init(&retainer,1) == SALTS_OK);
    f->config.retainer = &retainer; f->config.shell_close_budget_ms = 0;
    n.parameters = &p; n.parameter_count = 1;
    check(praktor_c11_session_init(&f->session,&f->config,&n,1) == SALTS_OK);
    check(drive(f) == SALTS_BT_RUNNING);
    r = praktor_c11_session_cancel(&f->session); check(r == SALTS_EBUSY || r == SALTS_OK);
    destroy(f); free(f); drain(&retainer);
  }
}
