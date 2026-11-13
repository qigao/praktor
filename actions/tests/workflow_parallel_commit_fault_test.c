/* Test-only failed transaction-group publication. JsonParser and TaskRegistry
 * remain the real C11 providers; this wrapper proves no task output can be
 * published when the canonical root value group refuses admission. */
#include "workflow_parallel_commit.h"
#include <string.h>
#include <tinytest.h>

static bool reject_next_group;
static int fail_value_group(praktor_c11_value_txn *transactions,size_t count) {
    if(reject_next_group) {
        reject_next_group=false;
        return PRAKTOR_C11_VALUE_ENOMEM;
    }
    return praktor_c11_value_txn_commit_group(transactions,count);
}
#define praktor_c11_value_txn_commit_group fail_value_group
#include "workflow_parallel_commit.c"
#undef praktor_c11_value_txn_commit_group

static const char *source=
    "tasks:\n"
    "  - {name: one, command: echo one}\n"
    "  - {name: two, command: echo two}\n";
static praktor_workflow_task_registry *registry_for_test(void) {
    praktor_workflow_snapshot *snapshot=NULL;
    praktor_workflow_task_registry *owner=NULL;
    check(praktor_workflow_snapshot_create_inline(vstr_from_cstr(source),
        &snapshot,NULL,0)==SALTS_OK);
    check(praktor_workflow_task_registry_create(snapshot,&owner)==SALTS_OK);
    praktor_workflow_snapshot_destroy(snapshot);
    return owner;
}
spec("C11 Parallel failed root publication cannot publish staged typed outputs") {
    it("retains root, task statuses, and active branch handles when group OOMs") {
        praktor_workflow_task_registry *registry=registry_for_test();
        praktor_c11_value_store root={0};
        praktor_c11_value_txn branches[2]={{0},{0}};
        json_value_t *a=json_parse("{\"a\":1}",7u);
        json_value_t *b=json_parse("{\"b\":2}",7u);
        praktor_workflow_task_output_update updates[2]={{0u,a},{1u,b}};
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_RUNNING;
        const json_value_t *borrow=(const json_value_t *)registry;
        int present=1;
        check(a&&b);
        check(praktor_c11_value_store_init(&root,3u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&root,"seed","initial")==PRAKTOR_C11_VALUE_OK);
        for(size_t i=0;i<2u;++i)
            check(praktor_c11_value_txn_begin(&branches[i],&root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&branches[0],"left","a")==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&branches[1],"right","b")==PRAKTOR_C11_VALUE_OK);
        reject_next_group=true;
        check(praktor_c11_workflow_parallel_commit(registry,updates,branches,2u)==
            PRAKTOR_C11_PARALLEL_MEMORY);
        check(!reject_next_group);
        check(branches[0].impl&&branches[1].impl);
        check(praktor_c11_value_store_size(&root)==1u);
        check(praktor_c11_value_store_contains(&root,"left",&present)==
            PRAKTOR_C11_VALUE_OK&&present==0);
        for(size_t i=0;i<2u;++i) {
            check(praktor_workflow_task_registry_state(registry,i,&state)==SALTS_OK);
            check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
            check(praktor_workflow_task_registry_get_output(registry,i,
                vstr_from_cstr(i?"b":"a"),&borrow)==SALTS_EBUSY);
            check(borrow==(const json_value_t *)registry);
        }
        /* Retry with the SAME active transactions; both sides now commit. */
        check(praktor_c11_workflow_parallel_commit(registry,updates,branches,2u)==
            PRAKTOR_C11_PARALLEL_OK);
        check(!branches[0].impl&&!branches[1].impl);
        check(praktor_c11_value_store_size(&root)==3u);
        json_free(a);json_free(b);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_task_registry_destroy(registry);
    }
}
