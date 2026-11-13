/* Execution-level qualification slice: real Salts CFlow ManualExecutor
 * callbacks drive independent branch transaction owners. Publication uses the
 * already-qualified C11 Parallel group API only after all accepted callbacks
 * have been finalized. No duplicate task queue or threadpool is implemented.
 * Real worker/concurrent cancellation and native Process drain are separate. */
#include "workflow_parallel_commit.h"
#include <cflow/executor.h>
#include <string.h>
#include <tinytest.h>

typedef struct branch_probe {
    praktor_c11_value_txn *transaction;
    const char *key,*value;
    size_t position;
    unsigned *sequence_counter;
    unsigned observed_sequence;
    unsigned run_count,cancel_count,final_count;
    bool fail_run,succeeded;
} branch_probe;
static void execute_branch(void *user) {
    branch_probe *probe=(branch_probe *)user;
    ++probe->run_count;
    if(probe->sequence_counter)
        probe->observed_sequence=++(*probe->sequence_counter);
    if(probe->fail_run) {
        probe->succeeded=false;
        return;
    }
    probe->succeeded=praktor_c11_value_txn_set(
        probe->transaction,probe->key,probe->value)==PRAKTOR_C11_VALUE_OK;
}
static void cancel_branch(void *user) {
    branch_probe *probe=(branch_probe *)user;
    ++probe->cancel_count;
    probe->succeeded=false;
}
static void finalize_branch(void *user) {
    branch_probe *probe=(branch_probe *)user;
    ++probe->final_count;
}
static cflow_executor_task task_for(branch_probe *probe) {
    cflow_executor_task task={0};
    task.run=execute_branch;
    task.cancel=cancel_branch;
    task.finalize=finalize_branch;
    task.user=probe;
    return task;
}
static const char *workflow=
    "tasks:\n"
    "  - {name: left, command: echo left}\n"
    "  - {name: right, command: echo right}\n";
static praktor_workflow_task_registry *registry_for_test(void) {
    praktor_workflow_snapshot *snapshot=NULL;
    praktor_workflow_task_registry *registry=NULL;
    check(praktor_workflow_snapshot_create_inline(
        vstr_from_cstr(workflow),&snapshot,NULL,0)==SALTS_OK);
    check(praktor_workflow_task_registry_create(snapshot,&registry)==SALTS_OK);
    praktor_workflow_snapshot_destroy(snapshot);
    return registry;
}
static bool value_is(const praktor_c11_value_store *root,
                     const char *key,const char *expected) {
    char copy[64]={0};
    size_t size=0u;
    return praktor_c11_value_store_get(root,key,copy,sizeof copy,&size)==
        PRAKTOR_C11_VALUE_OK && size==strlen(expected)&&strcmp(copy,expected)==0;
}
static bool output_is(const praktor_workflow_task_registry *registry,
                      size_t task,const char *key,const char *expected) {
    const json_value_t *value=NULL;
    char *text;
    bool same=false;
    if(praktor_workflow_task_registry_get_output(registry,task,
        vstr_from_cstr(key),&value)!=SALTS_OK)return false;
    text=json_serialize(value,NULL);
    if(text) {
        same=strcmp(text,expected)==0;
        json_serialize_free(text);
    }
    return same;
}
spec("CFlow ManualExecutor driven C11 Parallel admission and owner publication") {
    it("publishes two settled branches in executor-driven callback order") {
        cflow_executor executor={0};
        cflow_executor_control control={0};
        praktor_workflow_task_registry *registry=registry_for_test();
        praktor_c11_value_store root={0};
        praktor_c11_value_txn txns[2]={{0},{0}};
        unsigned sequence=0u;
        branch_probe probes[2]={
            {.transaction=&txns[0],.key="left",.value="L",
             .sequence_counter=&sequence},
            {.transaction=&txns[1],.key="right",.value="R",
             .sequence_counter=&sequence}
        };
        json_value_t *a=json_parse("{\"value\":1}",11u);
        json_value_t *b=json_parse("{\"value\":2}",11u);
        praktor_workflow_task_output_update updates[2]={{0u,a},{1u,b}};
        check(a&&b);
        check(praktor_c11_value_store_init(&root,4u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(cflow_executor_manual_init_with_capacity(&executor,2u));
        check(cflow_executor_as_control(&executor,&control));
        for(size_t i=0;i<2u;++i) {
            cflow_executor_task task;
            check(praktor_c11_value_txn_begin(&txns[i],&root)==PRAKTOR_C11_VALUE_OK);
            task=task_for(&probes[i]);
            check(cflow_executor_try_post_task(&executor,&task)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(cflow_executor_run_ready(&executor)==2u);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        for(size_t i=0;i<2u;++i) {
            check(probes[i].run_count==1u);
            check(probes[i].cancel_count==0u);
            check(probes[i].final_count==1u);
            check(probes[i].succeeded);
            check(probes[i].observed_sequence==i+1u);
        }
        check(praktor_c11_workflow_parallel_commit(registry,updates,txns,2u)==
            PRAKTOR_C11_PARALLEL_OK);
        check(value_is(&root,"left","L")&&value_is(&root,"right","R"));
        check(praktor_workflow_task_registry_complete(registry,0u)==SALTS_OK);
        check(praktor_workflow_task_registry_complete(registry,1u)==SALTS_OK);
        check(output_is(registry,0u,"value","1"));
        check(output_is(registry,1u,"value","2"));
        cflow_executor_destroy(&executor);
        json_free(a);json_free(b);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_task_registry_destroy(registry);
    }
    it("cancel-before-run drains pending callbacks and publishes nothing") {
        cflow_executor executor={0};
        cflow_executor_control control={0};
        cflow_executor_protocol_stats stats={0};
        praktor_workflow_task_registry *registry=registry_for_test();
        praktor_c11_value_store root={0};
        praktor_c11_value_txn txns[2]={{0},{0}};
        branch_probe probes[2]={
            {.transaction=&txns[0],.key="left",.value="L"},
            {.transaction=&txns[1],.key="right",.value="R"}
        };
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_RUNNING;
        check(praktor_c11_value_store_init(&root,4u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(cflow_executor_manual_init_with_capacity(&executor,2u));
        check(cflow_executor_as_control(&executor,&control));
        for(size_t i=0;i<2u;++i) {
            cflow_executor_task task=task_for(&probes[i]);
            check(praktor_c11_value_txn_begin(&txns[i],&root)==PRAKTOR_C11_VALUE_OK);
            check(cflow_executor_try_post_task(&executor,&task)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(cflow_executor_control_shutdown(
            &control,CFLOW_EXECUTOR_SHUTDOWN_CANCEL_PENDING));
        check(cflow_executor_run_ready(&executor)==0u);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(cflow_executor_control_get_stats(&control,&stats));
        check(stats.cancelled==2u&&stats.completed==0u);
        for(size_t i=0;i<2u;++i) {
            check(probes[i].run_count==0u);
            check(probes[i].cancel_count==1u);
            check(probes[i].final_count==1u);
            check(!probes[i].succeeded);
            praktor_c11_value_txn_abort(&txns[i]);
            check(praktor_workflow_task_registry_state(registry,i,&state)==SALTS_OK);
            check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        }
        check(praktor_c11_value_store_size(&root)==0u);
        cflow_executor_destroy(&executor);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_task_registry_destroy(registry);
    }
    it("bounded queue rejection requires group abort before publication") {
        cflow_executor executor={0};
        cflow_executor_control control={0};
        praktor_workflow_task_registry *registry=registry_for_test();
        praktor_c11_value_store root={0};
        praktor_c11_value_txn txns[2]={{0},{0}};
        branch_probe probes[2]={
            {.transaction=&txns[0],.key="left",.value="L"},
            {.transaction=&txns[1],.key="right",.value="R",.fail_run=true}
        };
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_RUNNING;
        check(praktor_c11_value_store_init(&root,4u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(cflow_executor_manual_init_with_capacity(&executor,1u));
        check(cflow_executor_as_control(&executor,&control));
        for(size_t i=0;i<2u;++i)
            check(praktor_c11_value_txn_begin(&txns[i],&root)==PRAKTOR_C11_VALUE_OK);
        {
            cflow_executor_task a=task_for(&probes[0]),b=task_for(&probes[1]);
            check(cflow_executor_try_post_task(&executor,&a)==CFLOW_ADMISSION_ACCEPTED);
            check(cflow_executor_try_post_task(&executor,&b)==CFLOW_ADMISSION_FULL);
        }
        check(cflow_executor_control_shutdown(
            &control,CFLOW_EXECUTOR_SHUTDOWN_CANCEL_PENDING));
        check(probes[0].cancel_count==1u&&probes[0].final_count==1u);
        check(probes[1].run_count==0u&&probes[1].final_count==0u);
        for(size_t i=0;i<2u;++i)praktor_c11_value_txn_abort(&txns[i]);
        check(praktor_c11_value_store_size(&root)==0u);
        for(size_t i=0;i<2u;++i) {
            check(praktor_workflow_task_registry_state(registry,i,&state)==SALTS_OK);
            check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        }
        cflow_executor_destroy(&executor);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_task_registry_destroy(registry);
    }
    it("failed settled branch cannot trigger a partial group commit") {
        cflow_executor executor={0};
        praktor_workflow_task_registry *registry=registry_for_test();
        praktor_c11_value_store root={0};
        praktor_c11_value_txn txns[2]={{0},{0}};
        branch_probe probes[2]={
            {.transaction=&txns[0],.key="left",.value="L"},
            {.transaction=&txns[1],.key="right",.value="R",.fail_run=true}
        };
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_RUNNING;
        check(praktor_c11_value_store_init(&root,3u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(cflow_executor_manual_init_with_capacity(&executor,2u));
        for(size_t i=0;i<2u;++i) {
            cflow_executor_task task=task_for(&probes[i]);
            check(praktor_c11_value_txn_begin(&txns[i],&root)==PRAKTOR_C11_VALUE_OK);
            check(cflow_executor_try_post_task(&executor,&task)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(cflow_executor_run_ready(&executor)==2u);
        check(probes[0].run_count==1u&&probes[0].succeeded);
        check(probes[1].run_count==1u&&!probes[1].succeeded);
        check(probes[0].final_count==1u&&probes[1].final_count==1u);
        /* This is owner policy: a failed branch aborts ALL still-active
         * transactions; there is no partial success publication. */
        for(size_t i=0;i<2u;++i)praktor_c11_value_txn_abort(&txns[i]);
        check(praktor_c11_value_store_size(&root)==0u);
        for(size_t i=0;i<2u;++i) {
            check(praktor_workflow_task_registry_state(registry,i,&state)==SALTS_OK);
            check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        }
        cflow_executor_destroy(&executor);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_task_registry_destroy(registry);
    }

}
