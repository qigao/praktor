/* Inject application-side C11 YAML/AST construction failures.  The real
 * Salts YAML provider and the real CSTL-backed AST remain in use; this test
 * wraps only the Praktor module's boundary calls and scratch allocations. */
#include "yml/orch_yaml.h"
#include <salts/error_codes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <tinytest.h>
#include <tstr.h>

#define TRACKED_CAPACITY 512u
static void *tracked[TRACKED_CAPACITY];
static size_t live_count;
static unsigned failure_point,operations;
static bool did_inject;

static void track(void *pointer) {
    if (!pointer) return;
    if (live_count == TRACKED_CAPACITY) abort();
    tracked[live_count++] = pointer;
}
static void untrack(void *pointer) {
    size_t i;
    if (!pointer) return;
    for (i = 0; i < live_count; ++i) if (tracked[i] == pointer) {
        tracked[i] = tracked[--live_count];
        return;
    }
    /* All owned values originating within the parser must be observable. */
    abort();
}
static bool inject(void) {
    if (!failure_point || ++operations != failure_point) return false;
    did_inject = true;
    return true;
}
static void set_fault(unsigned point) {
    failure_point = point;
    operations = 0;
    did_inject = false;
}
static void *fault_calloc(size_t n, size_t size) {
    void *pointer = inject() ? NULL : calloc(n, size);
    track(pointer);
    return pointer;
}
static void *fault_malloc(size_t size) {
    void *pointer = inject() ? NULL : malloc(size);
    track(pointer);
    return pointer;
}
static void fault_free(void *pointer) {
    untrack(pointer);
    free(pointer);
}
static tstr fault_dup(const char *text) {
    tstr result = inject() ? NULL : tstr_dup(text);
    track(result);
    return result;
}
static void fault_string_free(tstr value) {
    untrack(value);
    tstr_free(value);
}
static int fault_yaml_scalar(praktor_yaml_node node,tstr *out,praktor_yaml_diagnostic *error) {
    int status;
    if (inject()) return SALTS_ENOMEM;
    status = praktor_yaml_scalar(node, out, error);
    if (status == SALTS_OK) track(*out);
    return status;
}
static int fault_yaml_key(praktor_yaml_node node,tstr *out,praktor_yaml_diagnostic *error) {
    int status;
    if (inject()) return SALTS_ENOMEM;
    status = praktor_yaml_key(node, out, error);
    if (status == SALTS_OK) track(*out);
    return status;
}
static int fault_yaml_location(praktor_yaml_node node,tstr *out) {
    int status;
    if (inject()) return SALTS_ENOMEM;
    status = praktor_yaml_location(node, out);
    if (status == SALTS_OK) track(*out);
    return status;
}
static int fault_yaml_get(praktor_yaml_node node,const char *key,praktor_yaml_node *out,
                          praktor_yaml_diagnostic *error) {
    if (inject()) return SALTS_ENOMEM;
    return praktor_yaml_get(node,key,out,error);
}
static int fault_publish(const praktor_orch_flat_node *nodes,size_t count,
                         praktor_orch_tree **out,char *error,size_t capacity) {
    if (inject()) {
        if (error && capacity) (void)snprintf(error,capacity,"Injected AST publication fault");
        return SALTS_ENOMEM;
    }
    return praktor_orch_from_flat_nodes(nodes,count,out,error,capacity);
}

#define calloc fault_calloc
#define malloc fault_malloc
#define free fault_free
#define tstr_dup fault_dup
#define tstr_free fault_string_free
#define praktor_yaml_scalar fault_yaml_scalar
#define praktor_yaml_key fault_yaml_key
#define praktor_yaml_location fault_yaml_location
#define praktor_yaml_get fault_yaml_get
#define praktor_orch_from_flat_nodes fault_publish
#include "../src/yml/orch_yaml.c"
#undef calloc
#undef malloc
#undef free
#undef tstr_dup
#undef tstr_free
#undef praktor_yaml_scalar
#undef praktor_yaml_key
#undef praktor_yaml_location
#undef praktor_yaml_get
#undef praktor_orch_from_flat_nodes

static void walk(const char *source,const char *kind,bool accepted) {
    praktor_yaml_document *document=NULL;
    praktor_orch_command command={0};
    praktor_orch_tree *saved=NULL;
    unsigned point;
    check(praktor_yaml_document_create(vstr_from_cstr(source),&document,NULL)==SALTS_OK);
    command.command=vstr_from_cstr("preserved");
    check(praktor_orch_from_command(&command,&saved,NULL,0)==SALTS_OK);
    for(point=1;point<256;++point) {
        praktor_orch_tree *out=saved;
        char error[160]={0};
        int status;
        bool injected_at_point;
        set_fault(point);
        if(kind) status=praktor_orch_from_flat_yaml(
            praktor_yaml_root(document),kind,&out,error,sizeof error);
        else status=praktor_orch_from_named_yaml(
            praktor_yaml_root(document),&out,error,sizeof error);
        injected_at_point=did_inject;
        set_fault(0);
        if(injected_at_point) {
            check(status!=SALTS_OK && out==saved);
            /* Every injected boundary error on a well-formed YAML input is
             * ENOMEM: silently converting a failed field read into EINVAL
             * obscures the allocator/provider failure category. */
            if(accepted)check(status==SALTS_ENOMEM);
        } else if(accepted) {
            check(status==SALTS_OK && out!=NULL && out!=saved);
            if(status==SALTS_OK)praktor_orch_tree_destroy(out);
        } else {
            check(status!=SALTS_OK && out==saved);
        }
        check(live_count==0);
        if(!injected_at_point) break;
    }
    check(point>1&&point<256);
    praktor_orch_tree_destroy(saved);
    praktor_yaml_document_destroy(document);
}
spec("C11 orchestration parser boundary faults") {
    it("rolls back nested named map, child sequence and text copies") {
        walk("Sequence:\n"
             "  children:\n"
             "    - Shell: {cmd: first, output_key: out}\n"
             "    - Inverter:\n"
             "        child:\n"
             "          - Shell: {cmd: second}\n"
             "          - Sleep: 3\n",NULL,true);
    }
    it("rolls back flat branching with condition, then and else") {
        walk("name: choose\n"
             "if: ready\n"
             "then: [{Shell: {cmd: yes}}, {Sleep: 1}]\n"
             "else: {Shell: {cmd: no}}\n","if",true);
    }
    it("preserves ENOMEM for mandatory flat fields and publication") {
        walk("name: choose\n"
             "if: ready\n"
             "then: {Shell: {cmd: run}}\n","if",true);
        walk("name: select\n"
             "switch: mode\n"
             "cases: [{Shell: {cmd: one}}, {Shell: {cmd: two}}]\n","switch",true);
        walk("name: delayed\n"
             "delay: 5\n"
             "child: {Shell: {cmd: run}}\n","delay",true);
    }
    it("does not treat failed optional while/max_iterations reads as absence") {
        walk("name: loop\n"
             "while: ready\n"
             "max_iterations: 7\n"
             "do: {Shell: {cmd: tick}}\n","while",true);
    }
    it("does not publish a partial set_variable when value retrieval fails") {
        walk("name: assign\n"
             "set_variable: result\n"
             "value: ready\n","set_variable",true);
    }
    it("rolls back named runner field and late-invalid parameters") {
        walk("name: task\n"
             "sequence:\n"
             "  - Shell: {cmd: okay}\n"
             "  - ParseRegex: {input_key: stdout, pattern: ok}\n","sequence",true);
        walk("Sequence:\n"
             "  child: {Shell: {cmd: good}}\n"
             "  bogus: wrong\n",NULL,false);
    }
}
