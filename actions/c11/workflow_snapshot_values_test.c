#include "workflow_snapshot_values.h"
#include "native_host.h"
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
    it("seeds the existing C11 Native Host before typed SetVariable dispatch") {
        praktor_workflow_snapshot *snapshot=snapshot_for(with_vars);
        praktor_c11_native_host host={0};
        praktor_c11_bt_host hooks={0};
        praktor_c11_value_store *values=NULL;
        praktor_c11_leaf_parameter params[2]={
            {"key",PRAKTOR_C11_LEAF_STRING,"result",0.0,false},
            {"value",PRAKTOR_C11_LEAF_STRING,"{MODE}",0.0,false}
        };
        praktor_c11_leaf_request request={
            0u,"SetVariable",params,2u,false,NULL
        };
        salts_bt_node_status status=SALTS_BT_FAILURE;
        check(praktor_c11_native_host_init(&host,4u,32u,64u)==SALTS_OK);
        values=praktor_c11_native_host_values(&host);
        check(values!=NULL);
        if(values) {
            check(praktor_c11_workflow_seed_root_text_variables(
                snapshot,values)==PRAKTOR_C11_VALUE_OK);
        }
        praktor_workflow_snapshot_destroy(snapshot);
        snapshot=NULL;
        check(praktor_c11_native_host_bind(&host,&hooks)==SALTS_OK);
        check(hooks.tick_typed_leaf!=NULL);
        if(hooks.tick_typed_leaf) {
            check(hooks.tick_typed_leaf(hooks.user,&request,&status)==SALTS_OK);
            check(status==SALTS_BT_SUCCESS);
        }
        check(contains(values,"result","fast"));
        check(contains(values,"REV","2"));
        praktor_c11_native_host_destroy(&host);
        check(host.impl==NULL);
    }

}


/* Distinct NativeHost slots are seeded from the canonical root + the selected
 * task.vars, without copying unrelated peer task locals back into the root. */
spec("C11 WorkflowContext native task scope and selected output ownership") {
    it("loads isolated NativeHosts and prepares only explicitly selected task outputs") {
        const char *yaml=
            "variables: {MODE: root, BASE: initial}\n"
            "tasks:\n"
            "  - name: first\n"
            "    vars: {MODE: alpha, LOCAL: secret}\n"
            "    command: echo first\n"
            "  - name: second\n"
            "    vars: {MODE: beta, PEER: mine}\n"
            "    command: echo second\n";
        const char *keys[]={"answer"};
        praktor_workflow_snapshot *snap=snapshot_for(yaml);
        praktor_c11_value_store root={0},stale={0};
        praktor_c11_native_host a={0},b={0};
        praktor_c11_bt_host hooks={0};
        praktor_c11_value_txn staged={0};
        praktor_c11_leaf_parameter params[2]={
            {"key",PRAKTOR_C11_LEAF_STRING,"answer",0.0,false},
            {"value",PRAKTOR_C11_LEAF_STRING,"{MODE}",0.0,false}
        };
        praktor_c11_leaf_request req={0u,"SetVariable",params,2u,false,NULL};
        salts_bt_node_status status=SALTS_BT_FAILURE;
        int present=1;
        check(praktor_c11_value_store_init(&root,8u,32u,64u)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&root,"SYS","global")==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_seed_root_text_variables(snap,&root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_native_host_init(&a,8u,32u,64u)==SALTS_OK);
        check(praktor_c11_native_host_init(&b,8u,32u,64u)==SALTS_OK);
        check(praktor_c11_value_store_set(
            praktor_c11_native_host_values(&a),"STALE","discard")==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_load_task_text_scope(snap,0u,&root,
            praktor_c11_native_host_values(&a))==PRAKTOR_C11_VALUE_OK);
        check(contains(praktor_c11_native_host_values(&a),"MODE","alpha"));
        check(contains(praktor_c11_native_host_values(&a),"LOCAL","secret"));
        check(contains(praktor_c11_native_host_values(&a),"SYS","global"));
        check(praktor_c11_value_store_contains(
            praktor_c11_native_host_values(&a),"STALE",&present)==PRAKTOR_C11_VALUE_OK&&present==0);
        check(contains(&root,"MODE","root"));
        check(praktor_c11_native_host_bind(&a,&hooks)==SALTS_OK);
        check(hooks.tick_typed_leaf(hooks.user,&req,&status)==SALTS_OK);
        check(status==SALTS_BT_SUCCESS);
        check(contains(praktor_c11_native_host_values(&a),"answer","alpha"));
        check(praktor_c11_workflow_prepare_task_text_outputs(
            &root,praktor_c11_native_host_values(&a),keys,1u,&staged)==PRAKTOR_C11_VALUE_OK);
        check(staged.impl!=NULL);
        check(praktor_c11_value_store_contains(&root,"answer",&present)==PRAKTOR_C11_VALUE_OK&&present==0);
        check(praktor_c11_value_txn_commit(&staged)==PRAKTOR_C11_VALUE_OK);
        check(staged.impl==NULL);
        check(contains(&root,"answer","alpha"));
        check(contains(&root,"MODE","root"));
        check(praktor_c11_value_store_contains(&root,"LOCAL",&present)==PRAKTOR_C11_VALUE_OK&&present==0);
        check(praktor_c11_workflow_load_task_text_scope(snap,1u,&root,
            praktor_c11_native_host_values(&b))==PRAKTOR_C11_VALUE_OK);
        praktor_workflow_snapshot_destroy(snap);
        check(contains(praktor_c11_native_host_values(&b),"MODE","beta"));
        check(contains(praktor_c11_native_host_values(&b),"PEER","mine"));
        check(contains(praktor_c11_native_host_values(&b),"answer","alpha"));
        check(praktor_c11_value_store_contains(
            praktor_c11_native_host_values(&b),"LOCAL",&present)==PRAKTOR_C11_VALUE_OK&&present==0);
        check(contains(praktor_c11_native_host_values(&a),"MODE","alpha"));
        praktor_c11_native_host_destroy(&b);
        praktor_c11_native_host_destroy(&a);
        praktor_c11_value_store_destroy(&root);
    }

    it("rejects incomplete outputs, duplicates and mismatched Host limits without mutation") {
        const char *yaml=
            "variables: {MODE: root}\n"
            "tasks:\n"
            "  - name: one\n"
            "    vars: {MODE: child}\n"
            "    command: echo one\n";
        const char *missing[]={"missing"};
        const char *dupe[]={"MODE","MODE"};
        praktor_workflow_snapshot *snap=snapshot_for(yaml);
        praktor_c11_value_store root={0},child={0},different={0};
        praktor_c11_value_txn out={0};
        check(praktor_c11_value_store_init(&root,4u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_seed_root_text_variables(snap,&root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_init(&child,4u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&child,"SAVED","before")==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_load_task_text_scope(snap,99u,&root,&child)==PRAKTOR_C11_VALUE_EINVAL);
        check(contains(&child,"SAVED","before"));
        check(praktor_c11_workflow_load_task_text_scope(snap,0u,&root,&child)==PRAKTOR_C11_VALUE_OK);
        check(contains(&child,"MODE","child"));
        check(praktor_c11_workflow_prepare_task_text_outputs(
            &root,&child,missing,1u,&out)==PRAKTOR_C11_VALUE_ENOTFOUND);
        check(out.impl==NULL&&contains(&root,"MODE","root"));
        check(praktor_c11_workflow_prepare_task_text_outputs(
            &root,&child,dupe,2u,&out)==PRAKTOR_C11_VALUE_EINVAL);
        check(out.impl==NULL);
        check(praktor_c11_value_store_init(&different,3u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&different,"SAVED","keep")==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_load_task_text_scope(
            snap,0u,&root,&different)==PRAKTOR_C11_VALUE_EINVAL);
        check(contains(&different,"SAVED","keep"));
        praktor_c11_value_store_destroy(&different);
        praktor_c11_value_store_destroy(&child);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_snapshot_destroy(snap);
    }
}
