#include "workflow_parallel_commit.h"
#include <string.h>
#include <tinytest.h>

static const char *source=
    "tasks:\n"
    "  - {name: one, command: echo one}\n"
    "  - {name: two, command: echo two}\n"
    "  - {name: three, command: echo three}\n";

static praktor_workflow_task_registry *make_registry(void) {
    praktor_workflow_snapshot *snapshot=NULL;
    praktor_workflow_task_registry *registry=NULL;
    check(praktor_workflow_snapshot_create_inline(
        vstr_from_cstr(source),&snapshot,NULL,0)==SALTS_OK);
    check(praktor_workflow_task_registry_create(snapshot,&registry)==SALTS_OK);
    praktor_workflow_snapshot_destroy(snapshot);
    return registry;
}
static json_value_t *parse(const char *text) {
    json_value_t *value=json_parse(text,strlen(text));
    check(value!=NULL);
    return value;
}
static bool value_is(const praktor_c11_value_store *store,
                     const char *key,const char *wanted) {
    char value[64]={0};
    size_t n=0;
    return praktor_c11_value_store_get(store,key,value,sizeof value,&n)==
        PRAKTOR_C11_VALUE_OK && n==strlen(wanted)&&strcmp(value,wanted)==0;
}
static bool output_is(const praktor_workflow_task_registry *registry,
                      size_t task,const char *key,const char *wanted) {
    const json_value_t *value=NULL;
    char *encoded;
    bool same=false;
    if(praktor_workflow_task_registry_get_output(registry,task,
        vstr_from_cstr(key),&value)!=SALTS_OK)return false;
    encoded=json_serialize(value,NULL);
    if(encoded){
        same=strcmp(encoded,wanted)==0;
        json_serialize_free(encoded);
    }
    return same;
}
static void abort_branches(praktor_c11_value_txn *branches,size_t count) {
    for(size_t i=0;i<count;++i)praktor_c11_value_txn_abort(&branches[i]);
}
spec("C11 Parallel owner-turn typed output + optimistic text group") {
    it("publishes disjoint branch text and structured outputs together") {
        praktor_workflow_task_registry *registry=make_registry();
        praktor_c11_value_store root={0};
        praktor_c11_value_txn txns[2]={{0},{0}};
        json_value_t *one=parse("{\"answer\":{\"value\":1}}");
        json_value_t *two=parse("{\"answer\":{\"value\":2}}");
        praktor_workflow_task_output_update outputs[2]={{0u,one},{1u,two}};
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_PENDING;
        const json_value_t *pending=(const json_value_t *)registry;
        check(praktor_c11_value_store_init(&root,4u,32u,64u)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&root,"seed","base")==PRAKTOR_C11_VALUE_OK);
        for(size_t i=0;i<2u;++i)
            check(praktor_c11_value_txn_begin(&txns[i],&root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&txns[0],"left","L")==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&txns[1],"right","R")==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_parallel_commit(registry,outputs,txns,2u)==
            PRAKTOR_C11_PARALLEL_OK);
        check(!txns[0].impl&&!txns[1].impl);
        json_free(one);json_free(two);
        check(value_is(&root,"seed","base")&&value_is(&root,"left","L")&&
              value_is(&root,"right","R"));
        for(size_t i=0;i<2u;++i) {
            check(praktor_workflow_task_registry_state(registry,i,&state)==SALTS_OK);
            check(state==PRAKTOR_WORKFLOW_TASK_RUNNING);
            check(praktor_workflow_task_registry_get_output(registry,i,
                vstr_from_cstr("answer"),&pending)==SALTS_EBUSY);
            check(pending==(const json_value_t *)registry);
            check(praktor_workflow_task_registry_complete(registry,i)==SALTS_OK);
        }
        check(output_is(registry,0u,"answer","{\"value\":1}"));
        check(output_is(registry,1u,"answer","{\"value\":2}"));
        check(praktor_workflow_task_registry_state(registry,2u,&state)==SALTS_OK);
        check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_task_registry_destroy(registry);
    }
    it("rejects write/write conflict without publishing either kind of output") {
        praktor_workflow_task_registry *registry=make_registry();
        praktor_c11_value_store root={0};
        praktor_c11_value_txn txns[2]={{0},{0}};
        json_value_t *a=parse("{\"left\":1}"),*b=parse("{\"right\":2}");
        praktor_workflow_task_output_update outputs[2]={{0u,a},{1u,b}};
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_RUNNING;
        check(praktor_c11_value_store_init(&root,3u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&root,"shared","base")==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_begin(&txns[0],&root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_begin(&txns[1],&root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&txns[0],"shared","a")==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&txns[1],"shared","b")==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_parallel_commit(registry,outputs,txns,2u)==
            PRAKTOR_C11_PARALLEL_CONFLICT);
        check(txns[0].impl&&txns[1].impl);
        check(value_is(&root,"shared","base"));
        for(size_t i=0;i<2u;++i) {
            check(praktor_workflow_task_registry_state(registry,i,&state)==SALTS_OK);
            check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        }
        abort_branches(txns,2u);
        json_free(a);json_free(b);
        praktor_workflow_task_registry_destroy(registry);
        praktor_c11_value_store_destroy(&root);
    }
    it("rejects group capacity exhaustion without partial output publication") {
        praktor_workflow_task_registry *registry=make_registry();
        praktor_c11_value_store root={0};
        praktor_c11_value_txn txns[2]={{0},{0}};
        json_value_t *a=parse("{\"left\":1}"),*b=parse("{\"right\":2}");
        praktor_workflow_task_output_update outputs[2]={{0u,a},{1u,b}};
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_RUNNING;
        int found=1;
        check(praktor_c11_value_store_init(&root,2u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&root,"seed","base")==PRAKTOR_C11_VALUE_OK);
        for(size_t i=0;i<2u;++i)
            check(praktor_c11_value_txn_begin(&txns[i],&root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&txns[0],"left","a")==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&txns[1],"right","b")==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_parallel_commit(registry,outputs,txns,2u)==
            PRAKTOR_C11_PARALLEL_CAPACITY);
        check(value_is(&root,"seed","base"));
        check(praktor_c11_value_store_size(&root)==1u);
        check(praktor_c11_value_store_contains(&root,"left",&found)==PRAKTOR_C11_VALUE_OK);
        check(found==0);
        for(size_t i=0;i<2u;++i) {
            check(praktor_workflow_task_registry_state(registry,i,&state)==SALTS_OK);
            check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        }
        abort_branches(txns,2u);
        json_free(a);json_free(b);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_task_registry_destroy(registry);
    }
    it("refuses duplicate task slots or invalid JSON before value commit") {
        praktor_workflow_task_registry *registry=make_registry();
        praktor_c11_value_store root={0};
        praktor_c11_value_txn txns[2]={{0},{0}};
        json_value_t *a=parse("{\"left\":1}"),*b=parse("{\"right\":2}");
        praktor_workflow_task_output_update updates[2]={{0u,a},{0u,b}};
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_RUNNING;
        check(praktor_c11_value_store_init(&root,3u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        for(size_t i=0;i<2u;++i)
            check(praktor_c11_value_txn_begin(&txns[i],&root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&txns[0],"left","a")==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&txns[1],"right","b")==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_parallel_commit(registry,updates,txns,2u)==
            PRAKTOR_C11_PARALLEL_INVALID);
        updates[1].task_index=1u;
        updates[1].updates=json_create_string("not-an-object");
        check(praktor_c11_workflow_parallel_commit(registry,updates,txns,2u)==
            PRAKTOR_C11_PARALLEL_INVALID);
        json_free((json_value_t *)updates[1].updates);
        check(txns[0].impl&&txns[1].impl);
        check(praktor_c11_value_store_size(&root)==0u);
        check(praktor_workflow_task_registry_state(registry,0u,&state)==SALTS_OK);
        check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        abort_branches(txns,2u);
        json_free(a);json_free(b);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_task_registry_destroy(registry);
    }
    it("refuses finalized tasks and cancelled transactions") {
        praktor_workflow_task_registry *registry=make_registry();
        praktor_c11_value_store root={0};
        praktor_c11_value_txn txns[2]={{0},{0}};
        json_value_t *a=parse("{\"left\":1}"),*b=parse("{\"right\":2}");
        praktor_workflow_task_output_update updates[2]={{0u,a},{1u,b}};
        check(praktor_c11_value_store_init(&root,3u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        for(size_t i=0;i<2u;++i)
            check(praktor_c11_value_txn_begin(&txns[i],&root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_workflow_task_registry_complete(registry,0u)==SALTS_OK);
        check(praktor_c11_workflow_parallel_commit(registry,updates,txns,2u)==
            PRAKTOR_C11_PARALLEL_BUSY);
        check(txns[0].impl&&txns[1].impl);
        check(praktor_workflow_task_registry_start(registry,0u)==SALTS_OK);
        praktor_c11_value_txn_abort(&txns[1]);
        check(praktor_c11_workflow_parallel_commit(registry,updates,txns,2u)==
            PRAKTOR_C11_PARALLEL_INVALID);
        check(txns[0].impl&&!txns[1].impl);
        praktor_c11_value_txn_abort(&txns[0]);
        json_free(a);json_free(b);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_task_registry_destroy(registry);
    }
}
