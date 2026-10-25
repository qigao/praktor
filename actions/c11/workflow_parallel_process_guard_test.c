/* Native Process admission quiescence controls typed/text Parallel publication.
 * Retainer reservations represent unretired work. A successful worker join is
 * not a substitute for acknowledging retained completion and native close. */
#include "workflow_parallel_process_guard.h"
#include <string.h>
#include <tinytest.h>

static const char *yaml=
    "tasks:\n"
    "  - {name: left, command: echo left}\n"
    "  - {name: right, command: echo right}\n";

typedef struct guard_fixture {
    praktor_workflow_task_registry *registry;
    praktor_c11_value_store root;
    praktor_c11_value_txn branches[2];
    cflow_process_retainer retainer;
    json_value_t *json[2];
    praktor_workflow_task_output_update updates[2];
} guard_fixture;

static void initialize(guard_fixture *f) {
    praktor_workflow_snapshot *snapshot=NULL;
    const char *a="{\"output\":{\"id\":1}}";
    const char *b="{\"output\":{\"id\":2}}";
    memset(f,0,sizeof(*f));
    check(praktor_workflow_snapshot_create_inline(
        vstr_from_cstr(yaml),&snapshot,NULL,0)==SALTS_OK);
    check(praktor_workflow_task_registry_create(
        snapshot,&f->registry)==SALTS_OK);
    praktor_workflow_snapshot_destroy(snapshot);
    check(praktor_c11_value_store_init(&f->root,4u,32u,32u)==
        PRAKTOR_C11_VALUE_OK);
    check(praktor_c11_value_store_set(&f->root,"seed","base")==
        PRAKTOR_C11_VALUE_OK);
    check(cflow_process_retainer_init(&f->retainer,2u)==SALTS_OK);
    f->json[0]=json_parse(a,strlen(a));
    f->json[1]=json_parse(b,strlen(b));
    check(f->json[0]&&f->json[1]);
    for(size_t i=0;i<2u;++i) {
        f->updates[i].task_index=i;
        f->updates[i].updates=f->json[i];
        check(praktor_c11_value_txn_begin(&f->branches[i],&f->root)==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&f->branches[i],
            i?"right":"left",i?"R":"L")==PRAKTOR_C11_VALUE_OK);
    }
}
static void dispose(guard_fixture *f) {
    for(size_t i=0;i<2u;++i) {
        praktor_c11_value_txn_abort(&f->branches[i]);
        json_free(f->json[i]);
    }
    check(cflow_process_retainer_destroy(&f->retainer)==SALTS_OK);
    praktor_c11_value_store_destroy(&f->root);
    praktor_workflow_task_registry_destroy(f->registry);
}
static bool stored(const praktor_c11_value_store *root,
                   const char *key,const char *expected) {
    char text[64]={0};
    size_t n=0u;
    return praktor_c11_value_store_get(root,key,text,sizeof text,&n)==
        PRAKTOR_C11_VALUE_OK&&n==strlen(expected)&&strcmp(text,expected)==0;
}
static bool output_is(const praktor_workflow_task_registry *owner,
                      size_t task,const char *key,const char *expected) {
    const json_value_t *view=NULL;char *text;bool equal=false;
    if(praktor_workflow_task_registry_get_output(owner,task,
        vstr_from_cstr(key),&view)!=SALTS_OK)return false;
    text=json_serialize(view,NULL);
    if(text) {
        equal=strcmp(text,expected)==0;
        json_serialize_free(text);
    }
    return equal;
}
spec("C11 Parallel native Process recovery owner commit barrier") {
    it("refuses all publication while one Process reservation is live") {
        guard_fixture f;
        cflow_process_retainer_token token={0};
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_RUNNING;
        size_t pending=SIZE_MAX;
        initialize(&f);
        check(cflow_process_retainer_reserve(&f.retainer,&token)==SALTS_OK);
        check(praktor_c11_workflow_parallel_commit_after_process_drain(
            f.registry,f.updates,f.branches,2u,&f.retainer,8u,&pending)==
            PRAKTOR_C11_PARALLEL_BUSY);
        check(pending==1u);
        check(stored(&f.root,"seed","base"));
        check(praktor_c11_value_store_size(&f.root)==1u);
        for(size_t i=0;i<2u;++i) {
            check(f.branches[i].impl!=NULL);
            check(praktor_workflow_task_registry_state(f.registry,i,&state)==SALTS_OK);
            check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        }
        check(cflow_process_retainer_release(&f.retainer,token)==SALTS_OK);
        check(praktor_c11_workflow_parallel_commit_after_process_drain(
            f.registry,f.updates,f.branches,2u,&f.retainer,8u,&pending)==
            PRAKTOR_C11_PARALLEL_OK);
        check(pending==0u);
        check(!f.branches[0].impl&&!f.branches[1].impl);
        check(stored(&f.root,"left","L")&&stored(&f.root,"right","R"));
        for(size_t i=0;i<2u;++i)
            check(praktor_workflow_task_registry_complete(f.registry,i)==SALTS_OK);
        check(output_is(f.registry,0u,"output","{\"id\":1}"));
        check(output_is(f.registry,1u,"output","{\"id\":2}"));
        dispose(&f);
    }
    it("requires every outstanding lease to retire, not just the first") {
        guard_fixture f;
        cflow_process_retainer_token tokens[2]={{0},{0}};
        size_t pending=SIZE_MAX;
        initialize(&f);
        for(size_t i=0;i<2u;++i)
            check(cflow_process_retainer_reserve(&f.retainer,&tokens[i])==SALTS_OK);
        check(praktor_c11_workflow_parallel_commit_after_process_drain(
            f.registry,f.updates,f.branches,2u,&f.retainer,8u,&pending)==
            PRAKTOR_C11_PARALLEL_BUSY);
        check(pending==2u);
        check(cflow_process_retainer_release(&f.retainer,tokens[0])==SALTS_OK);
        check(praktor_c11_workflow_parallel_commit_after_process_drain(
            f.registry,f.updates,f.branches,2u,&f.retainer,8u,&pending)==
            PRAKTOR_C11_PARALLEL_BUSY);
        check(pending==1u);
        check(praktor_c11_value_store_size(&f.root)==1u);
        check(f.branches[0].impl&&f.branches[1].impl);
        check(cflow_process_retainer_release(&f.retainer,tokens[1])==SALTS_OK);
        check(praktor_c11_workflow_parallel_commit_after_process_drain(
            f.registry,f.updates,f.branches,2u,&f.retainer,8u,&pending)==
            PRAKTOR_C11_PARALLEL_OK);
        check(pending==0u);
        dispose(&f);
    }
    it("rejects invalid recovery owner without consuming transactions") {
        guard_fixture f;
        size_t pending=SIZE_MAX;
        initialize(&f);
        check(praktor_c11_workflow_parallel_commit_after_process_drain(
            f.registry,f.updates,f.branches,2u,NULL,8u,&pending)==
            PRAKTOR_C11_PARALLEL_INVALID);
        check(pending==0u);
        check(praktor_c11_workflow_parallel_commit_after_process_drain(
            f.registry,f.updates,f.branches,2u,&f.retainer,0u,&pending)==
            PRAKTOR_C11_PARALLEL_INVALID);
        check(pending==0u);
        check(f.branches[0].impl&&f.branches[1].impl);
        check(praktor_c11_value_store_size(&f.root)==1u);
        dispose(&f);
    }
}


typedef struct strict_callback_probe {
    unsigned ran,cancelled,finalized;
} strict_callback_probe;
static void strict_run(void *user) {
    ++((strict_callback_probe *)user)->ran;
}
static void strict_cancel(void *user) {
    ++((strict_callback_probe *)user)->cancelled;
}
static void strict_finalize(void *user) {
    ++((strict_callback_probe *)user)->finalized;
}
static cflow_executor_task strict_task(strict_callback_probe *probe) {
    cflow_executor_task task={0};
    task.run=strict_run;
    task.cancel=strict_cancel;
    task.finalize=strict_finalize;
    task.user=probe;
    return task;
}
spec("Strict CFlow executor and Process Parallel owner publication barrier") {
    it("requires a sealed and drained Executor before committing branch state") {
        guard_fixture f;
        cflow_executor executor={0};
        cflow_executor_control control={0};
        strict_callback_probe probes[2]={{0},{0}};
        size_t pending=SIZE_MAX;
        initialize(&f);
        check(cflow_executor_manual_init_with_capacity(&executor,2u));
        check(cflow_executor_as_control(&executor,&control));
        for(size_t i=0;i<2u;++i) {
            cflow_executor_task task=strict_task(&probes[i]);
            check(cflow_executor_try_post_task(&executor,&task)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
            f.registry,f.updates,f.branches,2u,&control,&f.retainer,8u,
            &pending)==PRAKTOR_C11_PARALLEL_BUSY);
        check(praktor_c11_value_store_size(&f.root)==1u);
        check(cflow_executor_control_shutdown(
            &control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
            f.registry,f.updates,f.branches,2u,&control,&f.retainer,8u,
            &pending)==PRAKTOR_C11_PARALLEL_BUSY);
        check(cflow_executor_run_ready(&executor)==2u);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(probes[0].ran==1u&&probes[1].ran==1u);
        check(probes[0].finalized==1u&&probes[1].finalized==1u);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
            f.registry,f.updates,f.branches,2u,&control,&f.retainer,8u,
            &pending)==PRAKTOR_C11_PARALLEL_OK);
        check(pending==0u);
        check(!f.branches[0].impl&&!f.branches[1].impl);
        check(stored(&f.root,"left","L")&&stored(&f.root,"right","R"));
        cflow_executor_destroy(&executor);
        dispose(&f);
    }
    it("refuses cancelled branches even when all callbacks are settled") {
        guard_fixture f;
        cflow_executor executor={0};
        cflow_executor_control control={0};
        strict_callback_probe probes[2]={{0},{0}};
        size_t pending=SIZE_MAX;
        initialize(&f);
        check(cflow_executor_manual_init_with_capacity(&executor,2u));
        check(cflow_executor_as_control(&executor,&control));
        for(size_t i=0;i<2u;++i) {
            cflow_executor_task task=strict_task(&probes[i]);
            check(cflow_executor_try_post_task(&executor,&task)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(cflow_executor_control_shutdown(
            &control,CFLOW_EXECUTOR_SHUTDOWN_CANCEL_PENDING));
        check(cflow_executor_run_ready(&executor)==0u);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(probes[0].ran==0u&&probes[1].ran==0u);
        check(probes[0].cancelled==1u&&probes[1].cancelled==1u);
        check(probes[0].finalized==1u&&probes[1].finalized==1u);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
            f.registry,f.updates,f.branches,2u,&control,&f.retainer,8u,
            &pending)==PRAKTOR_C11_PARALLEL_CANCELLED);
        check(pending==0u);
        check(praktor_c11_value_store_size(&f.root)==1u);
        check(f.branches[0].impl&&f.branches[1].impl);
        cflow_executor_destroy(&executor);
        dispose(&f);
    }
    it("refuses a missing group branch after bounded Executor rejection") {
        guard_fixture f;
        cflow_executor executor={0};
        cflow_executor_control control={0};
        strict_callback_probe probes[2]={{0},{0}};
        size_t pending=SIZE_MAX;
        initialize(&f);
        check(cflow_executor_manual_init_with_capacity(&executor,1u));
        check(cflow_executor_as_control(&executor,&control));
        cflow_executor_task first=strict_task(&probes[0]);
        cflow_executor_task second=strict_task(&probes[1]);
        check(cflow_executor_try_post_task(&executor,&first)==CFLOW_ADMISSION_ACCEPTED);
        check(cflow_executor_try_post_task(&executor,&second)==CFLOW_ADMISSION_FULL);
        check(cflow_executor_control_shutdown(
            &control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        check(cflow_executor_run_ready(&executor)==1u);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
            f.registry,f.updates,f.branches,2u,&control,&f.retainer,8u,
            &pending)==PRAKTOR_C11_PARALLEL_INVALID);
        check(praktor_c11_value_store_size(&f.root)==1u);
        check(f.branches[0].impl&&f.branches[1].impl);
        cflow_executor_destroy(&executor);
        dispose(&f);
    }
}
spec("C11 per-worker bounded Process retainer cohort barrier") {
    it("fails closed for aliasing, saturation and unfinished recovery, then publishes atomically") {
        guard_fixture f;
        cflow_process_retainer local[2]={{0},{0}};
        cflow_process_retainer *owners[2]={&local[0],&local[1]};
        cflow_process_retainer *aliased[2]={&local[0],&local[0]};
        cflow_process_retainer_token held={0},overflow={0};
        cflow_executor executor={0};
        cflow_executor_control control={0};
        strict_callback_probe probes[2]={{0},{0}};
        const bool branch_succeeded[2]={true,true};
        size_t pending=SIZE_MAX,reaped=SIZE_MAX;
        initialize(&f);
        for(size_t i=0u;i<2u;++i)
            check(cflow_process_retainer_init(&local[i],1u)==SALTS_OK);
        check(cflow_executor_manual_init_with_capacity(&executor,2u));
        check(cflow_executor_as_control(&executor,&control));
        for(size_t i=0u;i<2u;++i) {
            cflow_executor_task task=strict_task(&probes[i]);
            check(cflow_executor_try_post_task(&executor,&task)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(cflow_process_retainer_reserve(&local[1],&held)==SALTS_OK);
        check(cflow_process_retainer_reserve(&local[1],&overflow)==SALTS_ENOBUFS);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drains(
            f.registry,f.updates,f.branches,2u,branch_succeeded,&control,owners,2u,8u,
            &pending)==PRAKTOR_C11_PARALLEL_BUSY);
        check(pending==0u); /* OPEN: must not touch retainer */
        check(praktor_c11_workflow_parallel_drain_process_retainers(
            aliased,2u,8u,&pending,&reaped)==PRAKTOR_C11_PARALLEL_INVALID);
        check(pending==0u&&reaped==0u);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drains(
            f.registry,f.updates,f.branches,2u,branch_succeeded,&control,owners,1u,8u,
            &pending)==PRAKTOR_C11_PARALLEL_INVALID);
        check(cflow_executor_control_shutdown(&control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        check(cflow_executor_run_ready(&executor)==2u);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drains(
            f.registry,f.updates,f.branches,2u,branch_succeeded,&control,owners,2u,8u,
            &pending)==PRAKTOR_C11_PARALLEL_BUSY);
        check(pending==1u);
        check(f.branches[0].impl&&f.branches[1].impl);
        check(praktor_c11_value_store_size(&f.root)==1u);
        check(cflow_process_retainer_release(&local[1],held)==SALTS_OK);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drains(
            f.registry,f.updates,f.branches,2u,branch_succeeded,&control,owners,2u,8u,
            &pending)==PRAKTOR_C11_PARALLEL_OK);
        check(pending==0u);
        check(!f.branches[0].impl&&!f.branches[1].impl);
        check(stored(&f.root,"left","L")&&stored(&f.root,"right","R"));
        /* Typed output reads are only admitted for terminal TaskRegistry
         * slots; the Owner must mark success AFTER atomic publication. */
        for(size_t i=0u;i<2u;++i)
            check(praktor_workflow_task_registry_complete(f.registry,i)==SALTS_OK);
        check(output_is(f.registry,0u,"output","{\"id\":1}"));
        check(output_is(f.registry,1u,"output","{\"id\":2}"));
        cflow_executor_destroy(&executor);
        for(size_t i=0u;i<2u;++i)
            check(cflow_process_retainer_destroy(&local[i])==SALTS_OK);
        dispose(&f);
    }
    it("reports optimistic group conflict without publishing any typed or text output") {
        guard_fixture f;
        cflow_process_retainer local[2]={{0},{0}};
        cflow_process_retainer *owners[2]={&local[0],&local[1]};
        cflow_executor executor={0};
        cflow_executor_control control={0};
        strict_callback_probe probes[2]={{0},{0}};
        const bool branch_succeeded[2]={true,true};
        size_t pending=SIZE_MAX;
        initialize(&f);
        for(size_t i=0u;i<2u;++i)
            check(cflow_process_retainer_init(&local[i],1u)==SALTS_OK);
        check(cflow_executor_manual_init_with_capacity(&executor,2u));
        check(cflow_executor_as_control(&executor,&control));
        for(size_t i=0u;i<2u;++i) {
            cflow_executor_task task=strict_task(&probes[i]);
            check(cflow_executor_try_post_task(&executor,&task)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(cflow_executor_control_shutdown(&control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        check(cflow_executor_run_ready(&executor)==2u);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        /* Conflict must touch a key this transaction actually writes.
         * An unrelated seed edit would not be a true optimistic conflict. */
        check(praktor_c11_value_store_set(&f.root,"left","external")==
              PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drains(
            f.registry,f.updates,f.branches,2u,branch_succeeded,&control,owners,2u,8u,
            &pending)==PRAKTOR_C11_PARALLEL_CONFLICT);
        check(pending==0u&&f.branches[0].impl&&f.branches[1].impl);
        check(stored(&f.root,"seed","base")&&stored(&f.root,"left","external"));
        check(praktor_c11_value_store_size(&f.root)==2u);
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_RUNNING;
        for(size_t i=0u;i<2u;++i) {
            check(praktor_workflow_task_registry_state(f.registry,i,&state)==SALTS_OK);
            check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        }
        cflow_executor_destroy(&executor);
        for(size_t i=0u;i<2u;++i)
            check(cflow_process_retainer_destroy(&local[i])==SALTS_OK);
        dispose(&f);
    }
    it("refuses cancelled pending callbacks without touching the worker retainers") {
        guard_fixture f;
        cflow_process_retainer local[2]={{0},{0}};
        cflow_process_retainer *owners[2]={&local[0],&local[1]};
        cflow_executor executor={0};
        cflow_executor_control control={0};
        strict_callback_probe probes[2]={{0},{0}};
        const bool branch_succeeded[2]={true,true};
        cflow_process_retainer_token hold={0};
        size_t pending=SIZE_MAX,reaped=SIZE_MAX;
        initialize(&f);
        for(size_t i=0u;i<2u;++i)
            check(cflow_process_retainer_init(&local[i],1u)==SALTS_OK);
        check(cflow_executor_manual_init_with_capacity(&executor,2u));
        check(cflow_executor_as_control(&executor,&control));
        for(size_t i=0u;i<2u;++i) {
            cflow_executor_task task=strict_task(&probes[i]);
            check(cflow_executor_try_post_task(&executor,&task)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(cflow_process_retainer_reserve(&local[1],&hold)==SALTS_OK);
        check(cflow_executor_control_shutdown(&control,CFLOW_EXECUTOR_SHUTDOWN_CANCEL_PENDING));
        check(cflow_executor_run_ready(&executor)==0u);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drains(
            f.registry,f.updates,f.branches,2u,branch_succeeded,&control,owners,2u,8u,
            &pending)==PRAKTOR_C11_PARALLEL_CANCELLED);
        check(pending==0u&&f.branches[0].impl&&f.branches[1].impl);
        check(praktor_c11_workflow_parallel_drain_process_retainers(
            owners,2u,8u,&pending,&reaped)==PRAKTOR_C11_PARALLEL_BUSY);
        check(pending==1u&&reaped==0u);
        check(cflow_process_retainer_release(&local[1],hold)==SALTS_OK);
        check(praktor_c11_workflow_parallel_drain_process_retainers(
            owners,2u,8u,&pending,&reaped)==PRAKTOR_C11_PARALLEL_OK);
        check(pending==0u&&praktor_c11_value_store_size(&f.root)==1u);
        cflow_executor_destroy(&executor);
        for(size_t i=0u;i<2u;++i)
            check(cflow_process_retainer_destroy(&local[i])==SALTS_OK);
        dispose(&f);
    }
}

spec("Semantic Worker Process failure blocks the entire Parallel group") {
    it("refuses completed-but-failed branches before touching Process retainers") {
        guard_fixture f;
        cflow_process_retainer local[2]={{0},{0}};
        cflow_process_retainer *owners[2]={&local[0],&local[1]};
        cflow_process_retainer_token hold={0};
        cflow_executor executor={0};
        cflow_executor_control control={0};
        strict_callback_probe probes[2]={{0},{0}};
        const bool branch_succeeded[2]={true,false};
        size_t pending=SIZE_MAX,reaped=SIZE_MAX;
        initialize(&f);
        for(size_t i=0u;i<2u;++i)
            check(cflow_process_retainer_init(&local[i],1u)==SALTS_OK);
        check(cflow_executor_manual_init_with_capacity(&executor,2u));
        check(cflow_executor_as_control(&executor,&control));
        for(size_t i=0u;i<2u;++i) {
            cflow_executor_task task=strict_task(&probes[i]);
            check(cflow_executor_try_post_task(&executor,&task)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(cflow_executor_control_shutdown(&control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        check(cflow_executor_run_ready(&executor)==2u);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(probes[0].finalized==1u&&probes[1].finalized==1u);
        check(cflow_process_retainer_reserve(&local[1],&hold)==SALTS_OK);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drains(
            f.registry,f.updates,f.branches,2u,branch_succeeded,&control,owners,2u,8u,
            &pending)==PRAKTOR_C11_PARALLEL_FAILED);
        /* No retainer poll and no partial typed/text output on semantic failure. */
        check(pending==0u&&f.branches[0].impl&&f.branches[1].impl);
        check(stored(&f.root,"seed","base"));
        check(praktor_c11_value_store_size(&f.root)==1u);
        check(cflow_process_retainer_release(&local[1],hold)==SALTS_OK);
        check(praktor_c11_workflow_parallel_drain_process_retainers(
            owners,2u,8u,&pending,&reaped)==PRAKTOR_C11_PARALLEL_OK);
        check(pending==0u&&reaped==0u);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drains(
            f.registry,f.updates,f.branches,2u,branch_succeeded,&control,owners,2u,8u,
            &pending)==PRAKTOR_C11_PARALLEL_FAILED);
        cflow_executor_destroy(&executor);
        for(size_t i=0u;i<2u;++i)
            check(cflow_process_retainer_destroy(&local[i])==SALTS_OK);
        dispose(&f);
    }
}
