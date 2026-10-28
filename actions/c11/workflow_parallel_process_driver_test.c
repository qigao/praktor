/* Real Process/NativeIO closing lease: no publication before the existing
 * SaltsUtils recovery owner acknowledges completion and destroys the Process.
 * This is the same C Process driver/retainer route used by the C11 Session,
 * never a synthetic "worker done therefore I/O done" shortcut.
 */
#include "workflow_parallel_process_guard.h"
#include "praktor/process_driver.h"
#include <salts/clock.h>
#include <salts/thread.h>
#include <string.h>
#include <tinytest.h>

static const char *workflow=
    "tasks:\n"
    "  - {name: shell, command: echo ready}\n";
static bool root_is(const praktor_c11_value_store *root,
                    const char *key,const char *expected) {
    char text[64]={0};size_t n=0;
    return praktor_c11_value_store_get(root,key,text,sizeof text,&n)==
        PRAKTOR_C11_VALUE_OK&&n==strlen(expected)&&strcmp(text,expected)==0;
}
spec("C11 Parallel real Process close and retainer acknowledgment barrier") {
    it("holds typed and text output until a closing child is truly reaped") {
        praktor_workflow_snapshot *snapshot=NULL;
        praktor_workflow_task_registry *registry=NULL;
        praktor_c11_value_store root={0};
        praktor_c11_value_txn branch={0};
        cflow_process_retainer retainer={0};
        cflow_process_retainer_token hold={0};
        praktor_process_driver driver={0};
        cmeta_process_options_t options;
        const char *output_text="{\"result\":true}";
        json_value_t *json=json_parse(output_text,strlen(output_text));
        praktor_workflow_task_output_update update={0u,json};
        bool done=false;
        size_t pending=0u;
        int commit_status;
        uint64_t started;
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_RUNNING;
        check(json!=NULL);
        check(praktor_workflow_snapshot_create_inline(vstr_from_cstr(workflow),
            &snapshot,NULL,0)==SALTS_OK);
        check(praktor_workflow_task_registry_create(snapshot,&registry)==SALTS_OK);
        praktor_workflow_snapshot_destroy(snapshot);
        check(praktor_c11_value_store_init(&root,2u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&root,"seed","base")==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_begin(&branch,&root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&branch,"done","yes")==PRAKTOR_C11_VALUE_OK);
        check(cflow_process_retainer_init(&retainer,2u)==SALTS_OK);
        cmeta_process_options_init(&options);
#if defined(_WIN32)
        options.program="ping -n 5 127.0.0.1 > NUL";
#else
        options.program="sleep 3";
#endif
        options.flags=SALTS_PROCESS_SHELL_COMMAND;
        options.timeout_ms=6000u;
        check(praktor_process_driver_start(&driver,&options,NULL,0u,128u,
            &retainer,NULL,NULL)==SALTS_OK);
        check(praktor_process_driver_tick(&driver,&done)==SALTS_OK&&!done);
        check(praktor_process_driver_close(&driver,0u)==SALTS_EBUSY);
        check(praktor_process_driver_release(&driver)==SALTS_OK);
        check(driver.impl==NULL);
        /* The callback-bearing native Process now lives in the sole recovery
         * owner. Worker cancellation does not authorize destroying it. */
        check(cflow_process_retainer_destroy(&retainer)==SALTS_EBUSY);
        /* Pin an additional reservation to make this rejection independent
         * of how quickly the already-closing OS child acknowledges teardown.
         * The real adopted Process remains under the retainer, never in a
         * temporary global worker or user callback after driver release. */
        check(cflow_process_retainer_reserve(&retainer,&hold)==SALTS_OK);
        commit_status=praktor_c11_workflow_parallel_commit_after_process_drain(
            registry,&update,&branch,1u,&retainer,8u,&pending);
        check(commit_status==PRAKTOR_C11_PARALLEL_BUSY);
        check(pending>=1u);
        check(branch.impl!=NULL);
        check(praktor_c11_value_store_size(&root)==1u);
        check(root_is(&root,"seed","base"));
        check(praktor_workflow_task_registry_state(registry,0u,&state)==SALTS_OK);
        check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        check(cflow_process_retainer_release(&retainer,hold)==SALTS_OK);
        started=cmeta_hrtime();
        do {
            commit_status=praktor_c11_workflow_parallel_commit_after_process_drain(
                registry,&update,&branch,1u,&retainer,64u,&pending);
            check(commit_status==PRAKTOR_C11_PARALLEL_BUSY||
                  commit_status==PRAKTOR_C11_PARALLEL_OK);
            if(commit_status==PRAKTOR_C11_PARALLEL_BUSY)cmeta_sleep_ms(2u);
        } while(commit_status==PRAKTOR_C11_PARALLEL_BUSY&&
                cmeta_hrtime()-started<UINT64_C(7000000000));
        check(commit_status==PRAKTOR_C11_PARALLEL_OK);
        check(pending==0u);
        check(branch.impl==NULL);
        check(praktor_process_driver_release(&driver)==SALTS_EINVAL);
        check(praktor_workflow_task_registry_complete(registry,0u)==SALTS_OK);
        check(root_is(&root,"done","yes"));
        {
            const json_value_t *typed=NULL;
            check(praktor_workflow_task_registry_get_output(registry,0u,
                vstr_from_cstr("result"),&typed)==SALTS_OK);
            check(typed&&json_type(typed)==JSON_BOOL&&json_bool(typed));
        }
        check(cflow_process_retainer_destroy(&retainer)==SALTS_OK);
        json_free(json);
        praktor_c11_value_txn_abort(&branch);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_task_registry_destroy(registry);
    }
}
