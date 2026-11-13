/* Faults only the workflow-to-existing-store admission glue.  The canonical
 * store's clone/atomic-batch operations remain real; ASan/UBSan additionally
 * covers allocations owned inside the canonical store implementation. */
#include "workflow_snapshot_values.h"
#include <stdlib.h>
#include <string.h>
#include <tinytest.h>

static unsigned fail_point, boundary;
static bool was_injected;
static bool inject(void) {
    if(!fail_point || ++boundary!=fail_point)return false;
    was_injected=true;
    return true;
}
static void fault_on(unsigned point) {
    fail_point=point;
    boundary=0u;
    was_injected=false;
}
static void *fault_calloc(size_t count,size_t bytes) {
    return inject()?NULL:calloc(count,bytes);
}
static int fault_clone(praktor_c11_value_store *to,
                       const praktor_c11_value_store *from) {
    return inject()?PRAKTOR_C11_VALUE_ENOMEM:
        praktor_c11_value_store_clone(to,from);
}
static int fault_batch(praktor_c11_value_store *store,
                       const praktor_c11_value_write *values,size_t count) {
    return inject()?PRAKTOR_C11_VALUE_ENOMEM:
        praktor_c11_value_store_set_many(store,values,count);
}
static int fault_replace_clone(praktor_c11_value_store *to,
                               const praktor_c11_value_store *from) {
    return inject()?PRAKTOR_C11_VALUE_ENOMEM:
        praktor_c11_value_store_replace_clone(to,from);
}
static int fault_txn_begin(praktor_c11_value_txn *txn,
                           praktor_c11_value_store *root) {
    return inject()?PRAKTOR_C11_VALUE_ENOMEM:
        praktor_c11_value_txn_begin(txn,root);
}
static int fault_txn_set(praktor_c11_value_txn *txn,
                         const char *key,const char *value) {
    return inject()?PRAKTOR_C11_VALUE_ENOMEM:
        praktor_c11_value_txn_set(txn,key,value);
}
#define calloc fault_calloc
#define praktor_c11_value_store_clone fault_clone
#define praktor_c11_value_store_set_many fault_batch
#define praktor_c11_value_store_replace_clone fault_replace_clone
#define praktor_c11_value_txn_begin fault_txn_begin
#define praktor_c11_value_txn_set fault_txn_set
#include "workflow_snapshot_values.c"
#undef calloc
#undef praktor_c11_value_store_clone
#undef praktor_c11_value_store_set_many
#undef praktor_c11_value_store_replace_clone
#undef praktor_c11_value_txn_begin
#undef praktor_c11_value_txn_set

static bool has(const praktor_c11_value_store *store,
                const char *key,const char *wanted) {
    char text[64]={0};
    size_t count=0;
    return praktor_c11_value_store_get(store,key,text,sizeof text,&count)==
            PRAKTOR_C11_VALUE_OK &&
        count==strlen(wanted)&&strcmp(text,wanted)==0;
}
static const char *source=
    "variables: {MODE: root, BASE: initial}\n"
    "tasks:\n"
    "  - name: work\n"
    "    vars: {MODE: task, LOCAL: unique}\n"
    "    command: echo run\n";

spec("C11 workflow text context admission faults") {
    it("keeps root text untouched for all injected task fork boundaries") {
        praktor_workflow_snapshot *snapshot=NULL;
        praktor_c11_value_store root={0};
        unsigned point;
        fault_on(0);
        check(praktor_workflow_snapshot_create_inline(
            vstr_from_cstr(source),&snapshot,NULL,0)==SALTS_OK);
        check(praktor_c11_value_store_init(&root,5u,32u,32u)==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&root,"SYS","present")==
            PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_seed_root_text_variables(snapshot,&root)==
            PRAKTOR_C11_VALUE_OK);
        for(point=1u;point<20u;++point) {
            praktor_c11_value_store child={0};
            int status;
            bool faulted;
            fault_on(point);
            status=praktor_c11_workflow_fork_task_text_variables(
                snapshot,0u,&root,&child);
            faulted=was_injected;
            fault_on(0);
            if(faulted) {
                check(status==PRAKTOR_C11_VALUE_ENOMEM);
                check(child.impl==NULL);
            } else {
                check(status==PRAKTOR_C11_VALUE_OK);
                check(child.impl!=NULL);
                check(has(&child,"MODE","task"));
                check(has(&child,"LOCAL","unique"));
                praktor_c11_value_store_destroy(&child);
            }
            check(has(&root,"MODE","root"));
            check(has(&root,"BASE","initial"));
            check(has(&root,"SYS","present"));
            if(!faulted)break;
        }
        check(point>1u&&point<20u);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_snapshot_destroy(snapshot);
    }
    it("leaves prepopulated root unchanged if seeding allocation or publication fails") {
        praktor_workflow_snapshot *snapshot=NULL;
        unsigned point;
        fault_on(0);
        check(praktor_workflow_snapshot_create_inline(
            vstr_from_cstr(source),&snapshot,NULL,0)==SALTS_OK);
        for(point=1u;point<20u;++point) {
            praktor_c11_value_store root={0};
            int status;
            bool faulted;
            check(praktor_c11_value_store_init(&root,5u,32u,32u)==
                PRAKTOR_C11_VALUE_OK);
            check(praktor_c11_value_store_set(&root,"MODE","system")==
                PRAKTOR_C11_VALUE_OK);
            fault_on(point);
            status=praktor_c11_workflow_seed_root_text_variables(snapshot,&root);
            faulted=was_injected;
            fault_on(0);
            if(faulted) {
                int present=1;
                check(status==PRAKTOR_C11_VALUE_ENOMEM);
                check(has(&root,"MODE","system"));
                check(praktor_c11_value_store_contains(&root,"BASE",&present)==
                    PRAKTOR_C11_VALUE_OK&&present==0);
            } else {
                check(status==PRAKTOR_C11_VALUE_OK);
                check(has(&root,"MODE","root"));
                check(has(&root,"BASE","initial"));
            }
            praktor_c11_value_store_destroy(&root);
            if(!faulted)break;
        }
        check(point>1u&&point<20u);
        praktor_workflow_snapshot_destroy(snapshot);
    }
}


spec("C11 task-scope and output transaction fault rollback") {
    it("preserves an initialized NativeHost store at every injected task-scope handoff") {
        praktor_workflow_snapshot *snapshot=NULL;
        praktor_c11_value_store root={0};
        unsigned point;
        fault_on(0);
        check(praktor_workflow_snapshot_create_inline(
            vstr_from_cstr(source),&snapshot,NULL,0)==SALTS_OK);
        check(praktor_c11_value_store_init(&root,8u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_seed_root_text_variables(
            snapshot,&root)==PRAKTOR_C11_VALUE_OK);
        for(point=1u;point<25u;++point) {
            praktor_c11_value_store host_slot={0};
            bool injected;
            int status;
            check(praktor_c11_value_store_init(&host_slot,8u,32u,32u)==PRAKTOR_C11_VALUE_OK);
            check(praktor_c11_value_store_set(
                &host_slot,"PRESERVE","old")==PRAKTOR_C11_VALUE_OK);
            fault_on(point);
            status=praktor_c11_workflow_load_task_text_scope(
                snapshot,0u,&root,&host_slot);
            injected=was_injected;
            fault_on(0);
            if(injected) {
                check(status==PRAKTOR_C11_VALUE_ENOMEM);
                check(has(&host_slot,"PRESERVE","old"));
            } else {
                int present=1;
                check(status==PRAKTOR_C11_VALUE_OK);
                check(has(&host_slot,"MODE","task"));
                check(has(&host_slot,"LOCAL","unique"));
                check(praktor_c11_value_store_contains(&host_slot,
                    "PRESERVE",&present)==PRAKTOR_C11_VALUE_OK&&present==0);
            }
            check(has(&root,"MODE","root"));
            praktor_c11_value_store_destroy(&host_slot);
            if(!injected)break;
        }
        check(point>2u&&point<25u);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_snapshot_destroy(snapshot);
    }

    it("aborts every scratch/txn failure with root and output handle unchanged") {
        praktor_workflow_snapshot *snapshot=NULL;
        praktor_c11_value_store root={0},task={0};
        const char *selected[]={"answer","other"};
        unsigned point;
        fault_on(0);
        check(praktor_workflow_snapshot_create_inline(
            vstr_from_cstr(source),&snapshot,NULL,0)==SALTS_OK);
        check(praktor_c11_value_store_init(&root,8u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_seed_root_text_variables(
            snapshot,&root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_fork_task_text_variables(
            snapshot,0u,&root,&task)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&task,"answer","one")==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&task,"other","two")==PRAKTOR_C11_VALUE_OK);
        for(point=1u;point<25u;++point) {
            praktor_c11_value_txn prepared={0};
            bool injected;
            int status,present=1;
            fault_on(point);
            status=praktor_c11_workflow_prepare_task_text_outputs(
                &root,&task,selected,2u,&prepared);
            injected=was_injected;
            fault_on(0);
            if(injected) {
                check(status==PRAKTOR_C11_VALUE_ENOMEM);
                check(prepared.impl==NULL);
            } else {
                check(status==PRAKTOR_C11_VALUE_OK);
                check(prepared.impl!=NULL);
                praktor_c11_value_txn_abort(&prepared);
            }
            check(has(&root,"MODE","root"));
            check(praktor_c11_value_store_contains(
                &root,"answer",&present)==PRAKTOR_C11_VALUE_OK&&present==0);
            check(praktor_c11_value_store_contains(
                &root,"other",&present)==PRAKTOR_C11_VALUE_OK&&present==0);
            if(!injected)break;
        }
        check(point>3u&&point<25u);
        praktor_c11_value_store_destroy(&task);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_snapshot_destroy(snapshot);
    }
}
