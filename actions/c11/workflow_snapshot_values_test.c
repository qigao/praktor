#include "workflow_snapshot_values.h"
#include <string.h>
#include <tinytest.h>

static praktor_workflow_snapshot *snapshot_for(const char *source) {
    praktor_workflow_snapshot *owner=NULL;
    char error[256]={0};
    const int status=praktor_workflow_snapshot_create_inline(
        vstr_from_cstr(source),&owner,error,sizeof error);
    info("snapshot status=%d, diagnostic=%s",status,error);
    check(status==SALTS_OK);
    return owner;
}
static bool contains(const praktor_c11_value_store *store,
                     const char *key,const char *value) {
    char copy[128]={0};
    size_t n=0;
    return praktor_c11_value_store_get(store,key,copy,sizeof copy,&n)==
             PRAKTOR_C11_VALUE_OK &&
        n==strlen(value)&&strcmp(copy,value)==0;
}
static const char *with_vars=
    "name: text context\n"
    "variables: {MODE: fast, REV: '2'}\n"
    "tasks:\n"
    "  - {name: run, command: echo test}\n";
static const char *no_vars=
    "tasks:\n"
    "  - {name: run, command: echo test}\n";

spec("C11 WorkflowContext root string admission") {
    it("uses the existing store to atomically overlay user variables") {
        praktor_c11_value_store store={0};
        praktor_workflow_snapshot *snap=snapshot_for(with_vars);
        check(praktor_c11_value_store_init(&store,4u,32u,64u)==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&store,"MODE","system")==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&store,"SYS","available")==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_seed_root_text_variables(snap,&store)==
            PRAKTOR_C11_VALUE_OK);
        praktor_workflow_snapshot_destroy(snap);
        check(contains(&store,"MODE","fast"));
        check(contains(&store,"REV","2"));
        check(contains(&store,"SYS","available"));
        check(praktor_c11_value_store_size(&store)==3u);
        praktor_c11_value_store_destroy(&store);
    }
    it("rolls back all writes if root context lacks capacity") {
        praktor_c11_value_store store={0};
        praktor_workflow_snapshot *snap=snapshot_for(with_vars);
        int exists=1;
        check(praktor_c11_value_store_init(&store,2u,32u,64u)==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&store,"SYS","persist")==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&store,"MODE","before")==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_seed_root_text_variables(snap,&store)==
            PRAKTOR_C11_VALUE_ENOSPC);
        check(contains(&store,"SYS","persist"));
        check(contains(&store,"MODE","before"));
        check(praktor_c11_value_store_contains(&store,"REV",&exists)==
            PRAKTOR_C11_VALUE_OK && exists==0);
        check(praktor_c11_value_store_size(&store)==2u);
        praktor_workflow_snapshot_destroy(snap);
        praktor_c11_value_store_destroy(&store);
    }
    it("does not partially publish when a value exceeds the configured bound") {
        praktor_c11_value_store store={0};
        praktor_workflow_snapshot *snap=snapshot_for(with_vars);
        int present=1;
        check(praktor_c11_value_store_init(&store,4u,32u,3u)==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&store,"MODE","old")==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_seed_root_text_variables(snap,&store)==
            PRAKTOR_C11_VALUE_ENOBUFS);
        check(contains(&store,"MODE","old"));
        check(praktor_c11_value_store_contains(&store,"REV",&present)==
            PRAKTOR_C11_VALUE_OK && present==0);
        praktor_workflow_snapshot_destroy(snap);
        praktor_c11_value_store_destroy(&store);
    }
    it("does not allocate context entries for an absent variable map") {
        praktor_c11_value_store store={0};
        praktor_workflow_snapshot *snap=snapshot_for(no_vars);
        check(praktor_c11_value_store_init(&store,2u,32u,32u)==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&store,"SYS","keep")==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_seed_root_text_variables(snap,&store)==
            PRAKTOR_C11_VALUE_OK);
        check(contains(&store,"SYS","keep"));
        check(praktor_c11_value_store_size(&store)==1u);
        check(praktor_c11_workflow_seed_root_text_variables(NULL,&store)==
            PRAKTOR_C11_VALUE_EINVAL);
        praktor_workflow_snapshot_destroy(snap);
        praktor_c11_value_store_destroy(&store);
    }
}
