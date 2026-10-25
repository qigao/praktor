/* Fault the orchestration aggregate at each public owner/handoff boundary.
 * Provider and CSTL/JSON implementations remain real and are freed by their
 * owners. Only this module's scratch is tracked, so an injected short read
 * must never publish a partly constructed workflow or retain borrowed YAML.
 */
#include "yml/workflow_snapshot.h"
#include "yml/yaml_document.h"
#include "yml/yaml_task_metadata.h"
#include "yml/yaml_workflow_metadata.h"
#include "yml/yaml_action_params.h"
#include "yml/orch_yaml.h"
#include <salts/error_codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tinytest.h>

#define MAX_SNAPSHOT_OWNERS 64u
static void *tracked[MAX_SNAPSHOT_OWNERS];
static size_t live_owners;
static unsigned fault_at, operation_count;
static bool did_inject;
static bool injected(void) {
    if(!fault_at||++operation_count!=fault_at)return false;
    did_inject=true;
    return true;
}
static void reset_fault(unsigned point) {
    fault_at=point;operation_count=0u;did_inject=false;
}
static void track(void *pointer) {
    if(!pointer)return;
    if(live_owners>=MAX_SNAPSHOT_OWNERS)abort();
    tracked[live_owners++]=pointer;
}
static void untrack(void *pointer) {
    size_t i;
    if(!pointer)return;
    for(i=0;i<live_owners;++i)if(tracked[i]==pointer) {
        tracked[i]=tracked[--live_owners];return;
    }
    abort();
}
static void *fault_calloc(size_t count,size_t size) {
    void *result=injected()?NULL:calloc(count,size);
    track(result);return result;
}
static void *fault_malloc(size_t size) {
    void *result=injected()?NULL:malloc(size);
    track(result);return result;
}
static void fault_free(void *pointer) {
    untrack(pointer);free(pointer);
}
static int fault_document(vstr source,praktor_yaml_document **out,
                          praktor_yaml_diagnostic *diagnostic) {
    if(injected()) {
        if(diagnostic&&diagnostic->text&&diagnostic->capacity)
            (void)snprintf(diagnostic->text,diagnostic->capacity,
                "Injected YAML document admission failure");
        return SALTS_ENOMEM;
    }
    return praktor_yaml_document_create(source,out,diagnostic);
}
static int fault_get(praktor_yaml_node map,const char *name,
                     praktor_yaml_node *out,praktor_yaml_diagnostic *diagnostic) {
    if(injected())return SALTS_ENOMEM;
    return praktor_yaml_get(map,name,out,diagnostic);
}
static int fault_workflow_meta(praktor_yaml_node node,json_value_t **out,
                               praktor_yaml_field_error *diagnostic) {
    if(injected())return SALTS_ENOMEM;
    return praktor_yaml_workflow_metadata_create(node,out,diagnostic);
}
static int fault_task_meta(praktor_yaml_node node,json_value_t **out,
                           praktor_yaml_field_error *diagnostic) {
    if(injected())return SALTS_ENOMEM;
    return praktor_yaml_task_metadata_create(node,out,diagnostic);
}
static int fault_action(praktor_yaml_node node,praktor_yaml_action_kind kind,
                        praktor_yaml_action_params **out,
                        praktor_yaml_field_error *diagnostic) {
    if(injected())return SALTS_ENOMEM;
    return praktor_yaml_action_params_create(node,kind,out,diagnostic);
}
static int fault_command_view(const praktor_yaml_action_params *owner,
                              praktor_yaml_command_view *out) {
    if(injected())return SALTS_ENOMEM;
    return praktor_yaml_action_command(owner,out);
}
static int fault_command_entry(const praktor_yaml_action_params *owner,
                               size_t index,vstr *out) {
    if(injected())return SALTS_ENOMEM;
    return praktor_yaml_action_argument(owner,index,out);
}
static int fault_command_ast(const praktor_orch_command *input,
                             praktor_orch_tree **out,char *error,size_t capacity) {
    if(injected()) {
        if(error&&capacity)(void)snprintf(error,capacity,"Injected Command AST failure");
        return SALTS_ENOMEM;
    }
    return praktor_orch_from_command(input,out,error,capacity);
}
static int fault_named_ast(praktor_yaml_node node,praktor_orch_tree **out,
                           char *error,size_t capacity) {
    if(injected()) {
        if(error&&capacity)(void)snprintf(error,capacity,"Injected named AST failure");
        return SALTS_ENOMEM;
    }
    return praktor_orch_from_named_yaml(node,out,error,capacity);
}
static int fault_flat_ast(praktor_yaml_node node,const char *type,
                          praktor_orch_tree **out,char *error,size_t capacity) {
    if(injected()) {
        if(error&&capacity)(void)snprintf(error,capacity,"Injected flat AST failure");
        return SALTS_ENOMEM;
    }
    return praktor_orch_from_flat_yaml(node,type,out,error,capacity);
}
static int fault_graph(const json_value_t *const *nodes,size_t count,
                       praktor_workflow_dependencies **out,
                       char *error,size_t capacity) {
    if(injected()) {
        if(error&&capacity)(void)snprintf(error,capacity,"Injected graph failure");
        return SALTS_ENOMEM;
    }
    return praktor_workflow_dependencies_create(nodes,count,out,error,capacity);
}

#define calloc fault_calloc
#define malloc fault_malloc
#define free fault_free
#define praktor_yaml_document_create fault_document
#define praktor_yaml_get fault_get
#define praktor_yaml_workflow_metadata_create fault_workflow_meta
#define praktor_yaml_task_metadata_create fault_task_meta
#define praktor_yaml_action_params_create fault_action
#define praktor_yaml_action_command fault_command_view
#define praktor_yaml_action_argument fault_command_entry
#define praktor_orch_from_command fault_command_ast
#define praktor_orch_from_named_yaml fault_named_ast
#define praktor_orch_from_flat_yaml fault_flat_ast
#define praktor_workflow_dependencies_create fault_graph
#include "../src/yml/workflow_snapshot.c"
#undef calloc
#undef malloc
#undef free
#undef praktor_yaml_document_create
#undef praktor_yaml_get
#undef praktor_yaml_workflow_metadata_create
#undef praktor_yaml_task_metadata_create
#undef praktor_yaml_action_params_create
#undef praktor_yaml_action_command
#undef praktor_yaml_action_argument
#undef praktor_orch_from_command
#undef praktor_orch_from_named_yaml
#undef praktor_orch_from_flat_yaml
#undef praktor_workflow_dependencies_create

/* Exercises successively: Document -> Workflow metadata -> Command list and
 * postprocessor -> named AST -> flat AST -> graph publication. */
static const char *source=
    "name: injected-workflow\n"
    "tasks:\n"
    "  - name: prepare\n"
    "    command: [echo first, echo second]\n"
    "    parse_json: {path: '$.ready', output_key: ready}\n"
    "  - name: named\n"
    "    depends_on: [prepare]\n"
    "    actions:\n"
    "      Sequence:\n"
    "        - Shell: {cmd: hello}\n"
    "        - Sleep: 1\n"
    "  - name: flat\n"
    "    depends_on: [prepare, named]\n"
    "    if: ready\n"
    "    then: {Shell: {cmd: yes}}\n"
    "    else: {Shell: {cmd: no}}\n";

spec("C11 workflow snapshot owner boundary rollback") {
    it("retains existing snapshot and frees scratch at each failure boundary") {
        praktor_workflow_snapshot *saved=NULL;
        unsigned point;
        size_t existing;
        reset_fault(0);
        check(praktor_workflow_snapshot_create_inline(
            vstr_from_cstr(source),&saved,NULL,0)==SALTS_OK);
        check(saved!=NULL&&praktor_workflow_snapshot_task_count(saved)==3u);
        existing=live_owners;
        check(existing>0u);
        for(point=1;point<128u;++point) {
            praktor_workflow_snapshot *out=saved;
            char diagnostic[256]={0};
            int status;
            bool failed;
            reset_fault(point);
            status=praktor_workflow_snapshot_create_inline(
                vstr_from_cstr(source),&out,diagnostic,sizeof diagnostic);
            failed=did_inject;
            reset_fault(0);
            if(failed) {
                check(status==SALTS_ENOMEM);
                check(out==saved);
                check(diagnostic[0]!='\0');
            } else {
                check(status==SALTS_OK);
                check(out!=NULL&&out!=saved);
                if(status==SALTS_OK)
                    praktor_workflow_snapshot_destroy(out);
            }
            check(live_owners==existing);
            check(praktor_workflow_snapshot_task_count(saved)==3u);
            if(!failed)break;
        }
        check(point>1u&&point<128u);
        praktor_workflow_snapshot_destroy(saved);
        check(live_owners==0u);
    }
    it("does not publish a partial owner after semantic graph refusal") {
        const char *cycle=
            "tasks:\n"
            "  - {name: first, command: echo one, depends_on: [second]}\n"
            "  - {name: second, command: echo two, depends_on: [first]}\n";
        praktor_workflow_snapshot *saved=NULL,*out=NULL;
        char error[256]={0};
        reset_fault(0);
        check(praktor_workflow_snapshot_create_inline(
            vstr_from_cstr(source),&saved,NULL,0)==SALTS_OK);
        out=saved;
        const size_t owners=live_owners;
        check(praktor_workflow_snapshot_create_inline(vstr_from_cstr(cycle),
            &out,error,sizeof error)==SALTS_EINVAL);
        check(out==saved);
        check(strcmp(error,"Cycle detected in workflow dependencies")==0);
        check(live_owners==owners);
        praktor_workflow_snapshot_destroy(saved);
        check(live_owners==0u);
    }
}
