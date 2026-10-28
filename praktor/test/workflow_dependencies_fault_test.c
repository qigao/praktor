/* Exhaust the C11 dependency graph's allocation/copy/rehash/edge/generation
 * boundaries against real Salts JSON values. No provider/runtime allocator
 * is patched, and the validator is the same source as production opt-in. */
#include "yml/workflow_dependencies.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <tinytest.h>

#define MAX_TRACKED 128u
static void *owners[MAX_TRACKED];
static size_t live_count;
static unsigned fail_at,operation;
static bool injected;
static bool should_fail(void) {
    if(!fail_at||++operation!=fail_at)return false;
    injected=true;
    return true;
}
static void track(void *p) {
    if(!p)return;
    if(live_count>=MAX_TRACKED)abort();
    owners[live_count++]=p;
}
static void forget(void *p) {
    size_t i;
    if(!p)return;
    for(i=0;i<live_count;++i)if(owners[i]==p) {
        owners[i]=owners[--live_count];
        return;
    }
    abort();
}
static void *test_malloc(size_t size) {
    void *p=should_fail()?NULL:malloc(size);
    track(p);return p;
}
static void *test_calloc(size_t n,size_t size) {
    void *p=should_fail()?NULL:calloc(n,size);
    track(p);return p;
}
static void *test_realloc(void *p,size_t size) {
    void *res;
    if(should_fail())return NULL;
    res=realloc(p,size);
    if(res) {forget(p);track(res);}
    return res;
}
static void test_free(void *p) {
    forget(p);free(p);
}
static void fail_on(unsigned point) {
    fail_at=point;operation=0u;injected=false;
}
#define malloc test_malloc
#define calloc test_calloc
#define realloc test_realloc
#define free test_free
#include "../src/yml/workflow_dependencies.c"
#undef malloc
#undef calloc
#undef realloc
#undef free

static json_value_t *task(const char *name,const char *dependency) {
    json_value_t *object=json_create_object(),*deps;
    check(object!=NULL);
    check(json_object_add_n(object,"name",4u,json_create_string(name)));
    if(dependency) {
        deps=json_create_array();
        check(deps!=NULL);
        check(json_array_add_checked(deps,json_create_string(dependency)));
        check(json_object_add_n(object,"depends_on",10u,deps));
    }
    return object;
}
spec("C-only workflow dependency graph allocation and rollback") {
    it("preserves prior graph through every injected allocation failure") {
        json_value_t *items[4]={
            task("prepare",NULL),
            task("build","prepare"),
            task("test","build"),
            task("publish","test")};
        const json_value_t *inputs[4]={items[0],items[1],items[2],items[3]};
        praktor_workflow_dependencies *saved=NULL;
        unsigned point;
        char error[256]={0};
        size_t retained;
        fail_on(0);
        check(praktor_workflow_dependencies_create(inputs,4u,&saved,
            error,sizeof error)==SALTS_OK);
        retained=live_count;
        check(retained>0);
        for(point=1;point<128;++point) {
            praktor_workflow_dependencies *out=saved;
            bool was_injected;
            int status;
            fail_on(point);
            status=praktor_workflow_dependencies_create(inputs,4u,&out,
                error,sizeof error);
            was_injected=injected;
            fail_on(0);
            if(was_injected) {
                check(status==SALTS_ENOMEM);
                check(out==saved);
                check(error[0]!='\0');
            } else {
                check(status==SALTS_OK && out!=saved);
                if(status==SALTS_OK)
                    praktor_workflow_dependencies_destroy(out);
            }
            check(live_count==retained);
            if(!was_injected)break;
        }
        check(point>1u&&point<128u);
        for(size_t i=0;i<4u;++i)json_free(items[i]);
        {
            vstr name={0};
            bool edge=false;
            check(praktor_workflow_dependencies_task_name(saved,3u,&name)==SALTS_OK);
            check(vstr_eq(name,vstr_from_cstr("publish")));
            check(praktor_workflow_dependencies_has_edge(saved,2u,3u,&edge)==SALTS_OK);
            check(edge);
        }
        praktor_workflow_dependencies_destroy(saved);
        check(live_count==0u);
    }
    it("rejects a cycle without leaking owners or overwriting a valid graph") {
        json_value_t *items[2]={task("a","b"),task("b","a")};
        json_value_t *valid=task("valid",NULL);
        const json_value_t *invalid_inputs[2]={items[0],items[1]};
        const json_value_t *valid_input[1]={valid};
        praktor_workflow_dependencies *saved=NULL,*out;
        char error[256]={0};
        fail_on(0);
        check(praktor_workflow_dependencies_create(
            valid_input,1u,&saved,NULL,0)==SALTS_OK);
        out=saved;
        const size_t retained=live_count;
        check(praktor_workflow_dependencies_create(
            invalid_inputs,2u,&out,error,sizeof error)==SALTS_EINVAL);
        check(strcmp(error,"Cycle detected in workflow dependencies")==0);
        check(out==saved&&live_count==retained);
        praktor_workflow_dependencies_destroy(saved);
        check(live_count==0u);
        json_free(items[0]);json_free(items[1]);json_free(valid);
    }
}
