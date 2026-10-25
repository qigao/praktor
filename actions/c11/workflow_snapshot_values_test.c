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
    it("forks independent task-local text views without writing to root or peers") {
        const char *yaml=
            "variables: {MODE: root, BASE: initial}\n"
            "tasks:\n"
            "  - name: one\n"
            "    vars: {MODE: task-one, LOCAL: own}\n"
            "    command: echo one\n"
            "  - name: two\n"
            "    vars: {MODE: task-two, PEER: mine}\n"
            "    command: echo two\n";
        praktor_workflow_snapshot *snapshot=snapshot_for(yaml);
        praktor_c11_value_store root={0},a={0},b={0};
        check(praktor_c11_value_store_init(&root,5u,32u,32u)==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&root,"SYS","global")==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_seed_root_text_variables(snapshot,&root)==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_fork_task_text_variables(snapshot,0u,
            &root,&a)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_fork_task_text_variables(snapshot,1u,
            &root,&b)==PRAKTOR_C11_VALUE_OK);
        praktor_workflow_snapshot_destroy(snapshot);
        praktor_c11_value_store_destroy(&root);
        check(contains(&a,"MODE","task-one"));
        check(contains(&a,"BASE","initial"));
        check(contains(&a,"LOCAL","own"));
        check(contains(&a,"SYS","global"));
        check(contains(&b,"MODE","task-two"));
        check(contains(&b,"PEER","mine"));
        {
            int present=1;
            check(praktor_c11_value_store_contains(&b,"LOCAL",&present)==
                PRAKTOR_C11_VALUE_OK&&present==0);
        }
        check(praktor_c11_value_store_set(&a,"LOCAL","changed")==
            PRAKTOR_C11_VALUE_OK);
        check(contains(&a,"LOCAL","changed"));
        check(contains(&b,"MODE","task-two"));
        praktor_c11_value_store_destroy(&b);
        praktor_c11_value_store_destroy(&a);
    }
    it("fails a task fork atomically when the inherited store is full") {
        const char *yaml=
            "variables: {MODE: root, BASE: initial}\n"
            "tasks:\n"
            "  - name: one\n"
            "    vars: {MODE: task-one, LOCAL: new}\n"
            "    command: echo one\n";
        praktor_workflow_snapshot *snapshot=snapshot_for(yaml);
        praktor_c11_value_store root={0},child={0};
        int present=1;
        check(praktor_c11_value_store_init(&root,3u,32u,32u)==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&root,"SYS","original")==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_seed_root_text_variables(snapshot,&root)==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_fork_task_text_variables(snapshot,0u,
            &root,&child)==PRAKTOR_C11_VALUE_ENOSPC);
        check(child.impl==NULL);
        check(contains(&root,"MODE","root"));
        check(contains(&root,"BASE","initial"));
        check(contains(&root,"SYS","original"));
        check(praktor_c11_value_store_contains(&root,"LOCAL",&present)==
            PRAKTOR_C11_VALUE_OK&&present==0);
        praktor_workflow_snapshot_destroy(snapshot);
        praktor_c11_value_store_destroy(&root);
    }
    it("refuses oversized task values, invalid indices and live output handles") {
        const char *yaml=
            "tasks:\n"
            "  - name: one\n"
            "    vars: {MODE: too-long}\n"
            "    command: echo one\n";
        praktor_workflow_snapshot *snapshot=snapshot_for(yaml);
        praktor_c11_value_store root={0},child={0};
        check(praktor_c11_value_store_init(&root,3u,32u,4u)==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&root,"MODE","root")==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_fork_task_text_variables(snapshot,0u,
            &root,&child)==PRAKTOR_C11_VALUE_ENOBUFS);
        check(child.impl==NULL);
        check(contains(&root,"MODE","root"));
        check(praktor_c11_workflow_fork_task_text_variables(snapshot,9u,
            &root,&child)==PRAKTOR_C11_VALUE_EINVAL);
        check(child.impl==NULL);
        check(praktor_c11_workflow_fork_task_text_variables(snapshot,0u,
            &root,&root)==PRAKTOR_C11_VALUE_EINVAL);
        check(praktor_c11_value_store_init(&child,1u,32u,32u)==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&child,"SAVED","here")==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_fork_task_text_variables(snapshot,0u,
            &root,&child)==PRAKTOR_C11_VALUE_EINVAL);
        check(contains(&child,"SAVED","here"));
        praktor_c11_value_store_destroy(&child);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_snapshot_destroy(snapshot);
    }

}
