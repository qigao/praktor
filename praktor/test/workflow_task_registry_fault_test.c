/* Test-only failure injector for the canonical C11 task registry owner.
 * JSON allocations/clones and C11 status storage use their real providers.
 * Faults are injected only at cross-owner admission/publish boundaries.
 */
#include "yml/workflow_task_registry.h"
#include <stdlib.h>
#include <string.h>
#include <tinytest.h>
#define MAX_TRACKED 256u
static void *tracked[MAX_TRACKED];
static size_t live_count;
static unsigned fail_point,boundary;
static bool injected;
static bool fail_here(void) {
    if(!fail_point||++boundary!=fail_point)return false;
    injected=true;return true;
}
static void fault_at(unsigned point) {
    fail_point=point;boundary=0u;injected=false;
}
static void remember(void *p) {
    if(!p)return;
    if(live_count>=MAX_TRACKED)abort();
    tracked[live_count++]=p;
}
static void release(void *p) {
    size_t i;
    if(!p)return;
    for(i=0;i<live_count;++i)if(tracked[i]==p) {
        tracked[i]=tracked[--live_count];return;
    }
    abort();
}
static void *fault_calloc(size_t count,size_t size) {
    void *p=fail_here()?NULL:calloc(count,size);
    remember(p);return p;
}
static void *fault_malloc(size_t size) {
    void *p=fail_here()?NULL:malloc(size);
    remember(p);return p;
}
static void fault_free(void *p) {
    release(p);free(p);
}
static json_value_t *fault_clone(const json_value_t *value) {
    return fail_here()?NULL:json_clone(value);
}
static json_value_t *fault_new_object(void) {
    return fail_here()?NULL:json_create_object();
}
static bool fault_json_publish(json_value_t *object,const char *key,
                               size_t count,json_value_t *value) {
    return fail_here()?false:json_object_add_n(object,key,count,value);
}

#define calloc fault_calloc
#define malloc fault_malloc
#define free fault_free
#define json_clone fault_clone
#define json_create_object fault_new_object
#define json_object_add_n fault_json_publish
#include "../src/yml/workflow_task_registry.c"
#undef calloc
#undef malloc
#undef free
#undef json_clone
#undef json_create_object
#undef json_object_add_n

static praktor_workflow_snapshot *make_snapshot(void) {
    const char *yaml=
        "tasks:\n"
        "  - {name: first, command: echo good}\n"
        "  - {name: second, command: echo other}\n";
    praktor_workflow_snapshot *snapshot=NULL;
    check(praktor_workflow_snapshot_create_inline(vstr_from_cstr(yaml),
        &snapshot,NULL,0)==SALTS_OK);
    return snapshot;
}
static bool readable(const praktor_workflow_task_registry *owner,
                     size_t task,const char *key,const char *expected) {
    const json_value_t *value=NULL;
    const char *text;
    if(praktor_workflow_task_registry_get_output(owner,task,
        vstr_from_cstr(key),&value)!=SALTS_OK)return false;
    text=json_string(value);
    return text&&strcmp(text,expected)==0;
}
spec("C11 typed task registry owner fault rollback") {
    it("retains a published registry at all allocation boundaries") {
        praktor_workflow_snapshot *snapshot=make_snapshot();
        praktor_workflow_task_registry *saved=NULL;
        size_t existing;
        unsigned point;
        fault_at(0u);
        check(praktor_workflow_task_registry_create(snapshot,&saved)==SALTS_OK);
        existing=live_count;
        check(existing>0u);
        for(point=1u;point<40u;++point) {
            praktor_workflow_task_registry *out=saved;
            int status;
            bool hit;
            fault_at(point);
            status=praktor_workflow_task_registry_create(snapshot,&out);
            hit=injected;
            fault_at(0u);
            if(hit) {
                check(status==SALTS_ENOMEM);
                check(out==saved);
            } else {
                check(status==SALTS_OK&&out!=saved);
                if(status==SALTS_OK)praktor_workflow_task_registry_destroy(out);
            }
            check(live_count==existing);
            check(praktor_workflow_task_registry_count(saved)==2u);
            if(!hit)break;
        }
        check(point>1u&&point<40u);
        praktor_workflow_snapshot_destroy(snapshot);
        praktor_workflow_task_registry_destroy(saved);
        check(live_count==0u);
    }
    it("keeps existing typed outputs and state at each clone/publish fault") {
        praktor_workflow_snapshot *snapshot=make_snapshot();
        praktor_workflow_task_registry *owner=NULL;
        json_value_t *old=json_create_string("old"),*newer=json_create_string("new");
        unsigned point;
        size_t initial_live;
        fault_at(0u);
        check(praktor_workflow_task_registry_create(snapshot,&owner)==SALTS_OK);
        praktor_workflow_snapshot_destroy(snapshot);
        check(old&&newer);
        check(praktor_workflow_task_registry_set_output(
            owner,0u,vstr_from_cstr("value"),old)==SALTS_OK);
        initial_live=live_count;
        for(point=1u;point<40u;++point) {
            int status;
            bool hit;
            fault_at(point);
            status=praktor_workflow_task_registry_set_output(
                owner,0u,vstr_from_cstr("value"),newer);
            hit=injected;
            fault_at(0u);
            if(hit) {
                check(status==SALTS_ENOMEM);
            } else {
                check(status==SALTS_OK);
                if(status==SALTS_OK) {
                    /* Restore without touching any borrowed JSON input. */
                    check(praktor_workflow_task_registry_set_output(
                        owner,0u,vstr_from_cstr("value"),old)==SALTS_OK);
                }
            }
            check(live_count==initial_live);
            if(!hit)break;
        }
        check(point>1u&&point<40u);
        json_free(old);json_free(newer);
        check(praktor_workflow_task_registry_complete(owner,0u)==SALTS_OK);
        check(readable(owner,0u,"value","old"));
        praktor_workflow_task_registry_destroy(owner);
        check(live_count==0u);
    }
    it("does not finalize failure if copying its immutable reason fails") {
        praktor_workflow_snapshot *snapshot=make_snapshot();
        praktor_workflow_task_registry *owner=NULL;
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_PENDING;
        fault_at(0u);
        check(praktor_workflow_task_registry_create(snapshot,&owner)==SALTS_OK);
        praktor_workflow_snapshot_destroy(snapshot);
        fault_at(1u);
        check(praktor_workflow_task_registry_fail(owner,0u,
            vstr_from_cstr("cause"))==SALTS_ENOMEM);
        check(injected);
        fault_at(0u);
        check(praktor_workflow_task_registry_state(owner,0u,&state)==SALTS_OK);
        check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        check(praktor_workflow_task_registry_fail(owner,0u,
            vstr_from_cstr("cause"))==SALTS_OK);
        check(praktor_workflow_task_registry_state(owner,0u,&state)==SALTS_OK);
        check(state==PRAKTOR_WORKFLOW_TASK_FAILED);
        praktor_workflow_task_registry_destroy(owner);
        check(live_count==0u);
    }
}
