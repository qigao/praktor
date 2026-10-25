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
