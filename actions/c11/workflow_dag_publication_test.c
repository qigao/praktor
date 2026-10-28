/* Source-only integration: actual YAML Snapshot -> DAG readiness -> real
 * CFlow WorkerExecutor -> canonical optimistic text and typed TaskRegistry
 * publication, gated by CFlow settlement and native Process retainer.
 *
 * Shell commands in the input are deliberately NOT executed here: these
 * are deterministic test callbacks, not an end-to-end NativeHost claim.
 * Production WorkflowExecutor/Context, triggers, Each and Process ownership
 * are unchanged. Neither worker calls DAG/TaskRegistry/root publication. */
#include "workflow_parallel_process_guard.h"
#include "workflow_snapshot_values.h"
#include "yml/workflow_dag_state.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <tinytest.h>

enum { ROOT_TASKS = 2u, ALL_TASKS = 3u };

static const char *workflow_yaml=
    "name: Owner-turn publication\n"
    "variables: {source_label: from_yaml}\n"
    "tasks:\n"
    "  - {name: prepare, command: echo prepare}\n"
    "  - {name: inspect, command: echo inspect}\n"
    "  - {name: publish, depends_on: [prepare, inspect], command: echo publish}\n";

typedef struct owner_fixture {
    praktor_workflow_snapshot *snapshot;
    praktor_workflow_dag_state *dag;
    praktor_workflow_task_registry *registry;
    praktor_c11_value_store root;
    cflow_process_retainer retainer;
    cflow_executor executor;
    cflow_executor_control control;
} owner_fixture;

typedef struct owner_branch {
    praktor_c11_value_txn *txn;
    const char *key,*value;
    bool force_failure;
    atomic_uint ran,cancelled,finalized;
    atomic_int outcome; /* 1 success; -1 failure; -2 cancellation; 0 pending */
} owner_branch;

static void branch_init(owner_branch *branch,praktor_c11_value_txn *txn,
                        const char *key,const char *value,bool fail) {
    memset(branch,0,sizeof(*branch));
    branch->txn=txn;branch->key=key;branch->value=value;branch->force_failure=fail;
    atomic_init(&branch->ran,0u);
    atomic_init(&branch->cancelled,0u);
    atomic_init(&branch->finalized,0u);
    atomic_init(&branch->outcome,0);
}
static void branch_run(void *user) {
    owner_branch *branch=(owner_branch *)user;
    const int result=praktor_c11_value_txn_set(
        branch->txn,branch->key,branch->value);
    atomic_fetch_add(&branch->ran,1u);
    atomic_store(&branch->outcome,
        branch->force_failure||result!=PRAKTOR_C11_VALUE_OK?-1:1);
}
static void branch_cancel(void *user) {
    owner_branch *branch=(owner_branch *)user;
    atomic_fetch_add(&branch->cancelled,1u);
    atomic_store(&branch->outcome,-2);
}
static void branch_finalize(void *user) {
    owner_branch *branch=(owner_branch *)user;
    atomic_fetch_add(&branch->finalized,1u);
}
static cflow_executor_task descriptor(owner_branch *branch) {
    cflow_executor_task task={0};
    task.run=branch_run;
    task.cancel=branch_cancel;
    task.finalize=branch_finalize;
    task.user=branch;
    return task;
}
static json_value_t *parse_json(const char *source) {
    json_value_t *json=json_parse(source,strlen(source));
    check(json!=NULL);
    return json;
}
static bool root_equals(const praktor_c11_value_store *root,
                        const char *key,const char *expected) {
    char copied[80]={0};
    size_t count=0u;
    return praktor_c11_value_store_get(root,key,copied,sizeof copied,&count)==
        PRAKTOR_C11_VALUE_OK&&count==strlen(expected)&&
        strcmp(copied,expected)==0;
}
static bool output_equals(const praktor_workflow_task_registry *registry,
                          size_t index,const char *key,const char *expected) {
    const json_value_t *value=NULL;
    char *text;
    bool equals=false;
    if(praktor_workflow_task_registry_get_output(registry,index,
        vstr_from_cstr(key),&value)!=SALTS_OK)return false;
    text=json_serialize(value,NULL);
    if(text) {
        equals=strcmp(text,expected)==0;
        json_serialize_free(text);
    }
    return equals;
}

static void fixture_init(owner_fixture *f) {
    char error[256]={0};
    const praktor_workflow_dependencies *graph;
    memset(f,0,sizeof(*f));
    check(praktor_workflow_snapshot_create_inline(
        vstr_from_cstr(workflow_yaml),&f->snapshot,error,sizeof error)==SALTS_OK);
    check(f->snapshot!=NULL&&error[0]=='\0');
    graph=praktor_workflow_snapshot_dependencies(f->snapshot);
    check(graph!=NULL);
    check(praktor_workflow_snapshot_task_count(f->snapshot)==ALL_TASKS);
    check(praktor_workflow_task_registry_create(f->snapshot,&f->registry)==SALTS_OK);
    check(praktor_workflow_dag_state_create(graph,2u,&f->dag)==SALTS_OK);
    check(praktor_c11_value_store_init(&f->root,8u,64u,64u)==
        PRAKTOR_C11_VALUE_OK);
    check(praktor_c11_value_store_set(&f->root,"system_value","before")==
        PRAKTOR_C11_VALUE_OK);
    check(praktor_c11_workflow_seed_root_text_variables(
        f->snapshot,&f->root)==PRAKTOR_C11_VALUE_OK);
    check(root_equals(&f->root,"source_label","from_yaml"));
    check(root_equals(&f->root,"system_value","before"));
    check(cflow_process_retainer_init(&f->retainer,2u)==SALTS_OK);
}
static void executor_start(owner_fixture *f,size_t workers,size_t capacity) {
    check(cflow_executor_worker_init_with_capacity(
        &f->executor,workers,capacity));
    check(cflow_executor_as_control(&f->executor,&f->control));
}
static void executor_drain(owner_fixture *f) {
    check(cflow_executor_control_shutdown(
        &f->control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
    check(cflow_executor_control_wait_idle(&f->control)==CFLOW_EXECUTOR_WAIT_IDLE);
}
static void executor_finish(owner_fixture *f) {
    cflow_executor_destroy(&f->executor);
    memset(&f->control,0,sizeof(f->control));
}
static void fixture_destroy(owner_fixture *f) {
    check(cflow_process_retainer_destroy(&f->retainer)==SALTS_OK);
    praktor_c11_value_store_destroy(&f->root);
    praktor_workflow_dag_state_destroy(f->dag);
    praktor_workflow_task_registry_destroy(f->registry);
    praktor_workflow_snapshot_destroy(f->snapshot);
}
static void assert_done(owner_branch *branch,int expected_outcome) {
    const unsigned ran=atomic_load(&branch->ran);
    const unsigned cancelled=atomic_load(&branch->cancelled);
    const unsigned finalized=atomic_load(&branch->finalized);
    const int outcome=atomic_load(&branch->outcome);
    check(ran==1u);
    check(cancelled==0u);
    check(finalized==1u);
    check(outcome==expected_outcome);
}
static void post(owner_fixture *f,owner_branch *branch) {
    cflow_executor_task task=descriptor(branch);
    check(cflow_executor_try_post_task(&f->executor,&task)==CFLOW_ADMISSION_ACCEPTED);
}
static void assert_task_state(const owner_fixture *f,size_t index,
                              praktor_workflow_task_state expected) {
    praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_PENDING;
    check(praktor_workflow_task_registry_state(f->registry,index,&state)==SALTS_OK);
    check(state==expected);
}
static void assert_dag(const owner_fixture *f,size_t active,size_t complete,
                       size_t ready,praktor_workflow_dag_terminal terminal,
                       bool all_succeeded) {
    praktor_workflow_dag_stats stats={0};
    check(praktor_workflow_dag_get_stats(f->dag,&stats)==SALTS_OK);
    check(stats.task_count==ALL_TASKS);
    check(stats.active==active);
    check(stats.completed==complete);
    check(stats.ready==ready);
    check(stats.terminal==terminal);
    check(stats.all_succeeded==all_succeeded);
}

spec("C11 YAML DAG owner-turn CFlow + Process + typed output publication") {
    it("withholds both text and typed output until callbacks and Process leases settle") {
        owner_fixture f;
        cflow_process_retainer_token token={0};
        praktor_c11_value_txn root_txns[ROOT_TASKS]={{0},{0}};
        praktor_c11_value_txn publish_txn={0};
        owner_branch roots[ROOT_TASKS],last;
        json_value_t *root_json[ROOT_TASKS]={
            parse_json("{\"stamp\":{\"id\":1}}"),
            parse_json("{\"stamp\":{\"id\":2}}")
        };
        json_value_t *last_json=NULL;
        praktor_workflow_task_output_update updates[ROOT_TASKS]={
            {0u,root_json[0]},{1u,root_json[1]}
        };
        praktor_workflow_task_output_update last_update={0};
        size_t ids[ROOT_TASKS]={SIZE_MAX,SIZE_MAX},count=0u,pending=SIZE_MAX;
        fixture_init(&f);
        assert_dag(&f,0u,0u,2u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false);
        check(praktor_workflow_dag_claim_ready(
            f.dag,ids,ROOT_TASKS,&count)==SALTS_OK);
        check(count==ROOT_TASKS&&ids[0]==0u&&ids[1]==1u);
        for(size_t i=0u;i<ROOT_TASKS;++i)
            check(praktor_c11_value_txn_begin(
                &root_txns[i],&f.root)==PRAKTOR_C11_VALUE_OK);
        branch_init(&roots[0],&root_txns[0],"prepare_done","yes",false);
        branch_init(&roots[1],&root_txns[1],"inspect_done","yes",false);
        check(cflow_process_retainer_reserve(&f.retainer,&token)==SALTS_OK);
        executor_start(&f,2u,2u);
        for(size_t i=0u;i<ROOT_TASKS;++i)post(&f,&roots[i]);
        /* The worker may execute now, but the owner has published nothing. */
        assert_dag(&f,2u,0u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false);
        check(praktor_c11_value_store_size(&f.root)==2u);
        assert_task_state(&f,0u,PRAKTOR_WORKFLOW_TASK_PENDING);
        assert_task_state(&f,1u,PRAKTOR_WORKFLOW_TASK_PENDING);
        executor_drain(&f);
        assert_done(&roots[0],1);assert_done(&roots[1],1);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
            f.registry,updates,root_txns,ROOT_TASKS,&f.control,&f.retainer,
            8u,&pending)==PRAKTOR_C11_PARALLEL_BUSY);
        check(pending==1u);
        check(root_txns[0].impl&&root_txns[1].impl);
        check(praktor_c11_value_store_size(&f.root)==2u);
        check(cflow_process_retainer_release(&f.retainer,token)==SALTS_OK);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
            f.registry,updates,root_txns,ROOT_TASKS,&f.control,&f.retainer,
            8u,&pending)==PRAKTOR_C11_PARALLEL_OK);
        check(pending==0u);
        check(root_txns[0].impl==NULL&&root_txns[1].impl==NULL);
        for(size_t i=0u;i<ROOT_TASKS;++i) {
            check(praktor_workflow_task_registry_complete(
                f.registry,ids[i])==SALTS_OK);
            check(praktor_workflow_dag_finish(f.dag,ids[i],true)==SALTS_OK);
        }
        json_free(root_json[0]);json_free(root_json[1]);
        check(root_equals(&f.root,"prepare_done","yes"));
        check(root_equals(&f.root,"inspect_done","yes"));
        check(output_equals(f.registry,0u,"stamp","{\"id\":1}"));
        check(output_equals(f.registry,1u,"stamp","{\"id\":2}"));
        assert_dag(&f,0u,2u,1u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false);
        executor_finish(&f);

        /* A distinct CFlow cohort follows only after atomic predecessor
         * publication. The root and typed registry survive the first pool. */
        count=0u;
        check(praktor_workflow_dag_claim_ready(
            f.dag,ids,ROOT_TASKS,&count)==SALTS_OK);
        check(count==1u&&ids[0]==2u);
        check(praktor_c11_value_txn_begin(
            &publish_txn,&f.root)==PRAKTOR_C11_VALUE_OK);
        branch_init(&last,&publish_txn,"publish_done","yes",false);
        last_json=parse_json("{\"stamp\":{\"id\":3}}");
        last_update.task_index=2u;
        last_update.updates=last_json;
        executor_start(&f,1u,1u);
        post(&f,&last);
        executor_drain(&f);
        assert_done(&last,1);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
            f.registry,&last_update,&publish_txn,1u,&f.control,&f.retainer,
            8u,&pending)==PRAKTOR_C11_PARALLEL_OK);
        check(praktor_workflow_task_registry_complete(f.registry,2u)==SALTS_OK);
        check(praktor_workflow_dag_finish(f.dag,2u,true)==SALTS_OK);
        json_free(last_json);
        check(root_equals(&f.root,"publish_done","yes"));
        check(output_equals(f.registry,2u,"stamp","{\"id\":3}"));
        assert_dag(&f,0u,ALL_TASKS,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,true);
        executor_finish(&f);
        fixture_destroy(&f);
    }

    it("aborts all branch transactions and suppresses descendants on a failed callback") {
        owner_fixture f;
        praktor_c11_value_txn txns[ROOT_TASKS]={{0},{0}};
        owner_branch roots[ROOT_TASKS];
        json_value_t *a=parse_json("{\"stamp\":1}");
        json_value_t *b=parse_json("{\"stamp\":2}");
        praktor_workflow_task_output_update pending[ROOT_TASKS]={{0u,a},{1u,b}};
        praktor_workflow_task_output_group *prepared=NULL;
        size_t ids[ROOT_TASKS]={SIZE_MAX,SIZE_MAX},count=0u;
        fixture_init(&f);
        check(praktor_workflow_dag_claim_ready(
            f.dag,ids,ROOT_TASKS,&count)==SALTS_OK);
        check(count==ROOT_TASKS&&ids[0]==0u&&ids[1]==1u);
        for(size_t i=0u;i<ROOT_TASKS;++i)
            check(praktor_c11_value_txn_begin(
                &txns[i],&f.root)==PRAKTOR_C11_VALUE_OK);
        branch_init(&roots[0],&txns[0],"prepare_done","yes",true);
        branch_init(&roots[1],&txns[1],"inspect_done","yes",false);
        /* Stage both typed objects before execution, without publishing.
         * The owner discards this group if any callback fails. */
        check(praktor_workflow_task_registry_prepare_output_group(
            f.registry,pending,ROOT_TASKS,&prepared)==SALTS_OK);
        check(prepared!=NULL);
        executor_start(&f,2u,2u);
        for(size_t i=0u;i<ROOT_TASKS;++i)post(&f,&roots[i]);
        executor_drain(&f);
        assert_done(&roots[0],-1);assert_done(&roots[1],1);
        /* Semantic application failure vetoes the group before any
         * transaction or typed TaskRegistry group is committed. */
        praktor_workflow_task_registry_discard_output_group(prepared);
        for(size_t i=0u;i<ROOT_TASKS;++i)
            praktor_c11_value_txn_abort(&txns[i]);
        check(praktor_workflow_task_registry_fail(
            f.registry,ids[0],vstr_from_cstr("injected branch failure"))==SALTS_OK);
        check(praktor_workflow_dag_finish(f.dag,ids[0],false)==SALTS_OK);
        check(praktor_workflow_dag_finish(f.dag,ids[1],true)==SALTS_OK);
        assert_dag(&f,0u,2u,0u,PRAKTOR_WORKFLOW_DAG_FAILED,false);
        check(praktor_workflow_dag_claim_ready(
            f.dag,ids,ROOT_TASKS,&count)==SALTS_EBUSY);
        check(count==0u);
        check(praktor_c11_value_store_size(&f.root)==2u);
        assert_task_state(&f,0u,PRAKTOR_WORKFLOW_TASK_FAILED);
        assert_task_state(&f,1u,PRAKTOR_WORKFLOW_TASK_PENDING);
        assert_task_state(&f,2u,PRAKTOR_WORKFLOW_TASK_PENDING);
        check(!output_equals(f.registry,0u,"stamp","1"));
        check(!output_equals(f.registry,1u,"stamp","2"));
        json_free(a);json_free(b);
        executor_finish(&f);
        fixture_destroy(&f);
    }
}
