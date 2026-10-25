#include "yml/workflow_snapshot.h"
#include <salts/error_codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tinytest.h>

static bool node_param_is(const praktor_orch_node *node,
                          const char *key,const char *value) {
    size_t i;
    for(i=0;i<praktor_orch_param_count(node);++i) {
        praktor_orch_param param={0};
        if(praktor_orch_param_at(node,i,&param)==SALTS_OK &&
           vstr_eq(param.key,vstr_from_cstr(key)) &&
           vstr_eq(param.value,vstr_from_cstr(value)))return true;
    }
    return false;
}
static bool task_name_is(const praktor_workflow_snapshot *snapshot,
                         size_t index,const char *wanted) {
    const json_value_t *task=praktor_workflow_snapshot_task_metadata(snapshot,index);
    const json_value_t *name=task?json_object_get(task,"name"):NULL;
    return name&&json_string(name)&&strcmp(json_string(name),wanted)==0;
}
static const char *mixed_source=
    "name: Native composition\n"
    "variables: {MODE: test}\n"
    "tasks:\n"
    "  - name: prepare\n"
    "    command: echo ready\n"
    "    parse_json: {path: '$.ready', output_key: parsed}\n"
    "  - name: run\n"
    "    depends_on: [prepare]\n"
    "    sequence:\n"
    "      - Shell: {cmd: echo run}\n"
    "      - FileExists: {path: marker}\n"
    "  - name: decide\n"
    "    depends_on: [prepare, run]\n"
    "    if: ready\n"
    "    then: {Shell: {cmd: echo yes}}\n"
    "    else: {Shell: {cmd: echo no}}\n";

spec("C11 inline workflow snapshot owner") {
    it("owns metadata, command AST, flat AST and DAG after source teardown") {
        char *source=(char *)malloc(strlen(mixed_source)+1u);
        praktor_workflow_snapshot *snapshot=NULL;
        const praktor_workflow_dependencies *graph;
        const praktor_orch_node *root=NULL,*first=NULL,*second=NULL;
        praktor_workflow_snapshot_task_kind kind;
        char error[512]={0};
        vstr name={0};
        bool found=false;
        check(source!=NULL);
        if(!source)return;
        strcpy(source,mixed_source);
        check(praktor_workflow_snapshot_create_inline(vstr_from_cstr(source),
            &snapshot,error,sizeof error)==SALTS_OK);
        free(source);source=NULL;
        check(snapshot!=NULL&&error[0]=='\0');
        if(!snapshot)return;
        check(praktor_workflow_snapshot_task_count(snapshot)==3u);
        check(task_name_is(snapshot,0u,"prepare"));
        check(task_name_is(snapshot,1u,"run"));
        check(task_name_is(snapshot,2u,"decide"));
        check(praktor_workflow_snapshot_task_kind_at(
            snapshot,0u,&kind)==SALTS_OK);
        check(kind==PRAKTOR_WORKFLOW_SNAPSHOT_COMMAND);
        check(praktor_workflow_snapshot_task_kind_at(
            snapshot,1u,&kind)==SALTS_OK);
        check(kind==PRAKTOR_WORKFLOW_SNAPSHOT_ORCHESTRATION);
        root=praktor_orch_tree_root(praktor_workflow_snapshot_task_tree(snapshot,0u));
        check(root!=NULL&&praktor_orch_node_type(root)==PRAKTOR_ORCH_SEQUENCE);
        check(praktor_orch_child_count(root)==2u);
        check(praktor_orch_child_at(root,0u,&first)==SALTS_OK);
        check(praktor_orch_node_type(first)==PRAKTOR_ORCH_SHELL);
        check(node_param_is(first,"cmd","echo ready"));
        check(node_param_is(first,"output_key","stdout"));
        check(praktor_orch_child_at(root,1u,&second)==SALTS_OK);
        check(praktor_orch_node_type(second)==PRAKTOR_ORCH_PARSE_JSON);
        check(node_param_is(second,"path","$.ready"));
        root=praktor_orch_tree_root(praktor_workflow_snapshot_task_tree(snapshot,1u));
        check(root!=NULL&&praktor_orch_node_type(root)==PRAKTOR_ORCH_SEQUENCE);
        check(praktor_orch_child_count(root)==2u);
        root=praktor_orch_tree_root(praktor_workflow_snapshot_task_tree(snapshot,2u));
        check(root!=NULL&&praktor_orch_node_type(root)==PRAKTOR_ORCH_IF_THEN_ELSE);
        check(node_param_is(root,"condition","ready"));
        check(praktor_orch_child_count(root)==2u);
        graph=praktor_workflow_snapshot_dependencies(snapshot);
        check(graph!=NULL);
        check(praktor_workflow_dependencies_task_count(graph)==3u);
        check(praktor_workflow_dependencies_edge_count(graph)==3u);
        check(praktor_workflow_dependencies_task_name(graph,2u,&name)==SALTS_OK);
        check(vstr_eq(name,vstr_from_cstr("decide")));
        check(praktor_workflow_dependencies_has_edge(graph,0u,2u,&found)==SALTS_OK);
        check(found);
        found=false;
        check(praktor_workflow_dependencies_has_edge(graph,1u,2u,&found)==SALTS_OK);
        check(found);
        praktor_workflow_snapshot_destroy(snapshot);
    }
    it("keeps prior owner when a later task fails or graph refuses") {
        praktor_workflow_snapshot *saved=NULL,*out;
        char error[512]={0};
        const char *sources[]={
            "tasks:\n"
            "  - {name: first, command: echo one}\n"
            "  - {name: second, sequence: [{Shell: {cmd: [bad]}}]}\n",
            "tasks:\n"
            "  - {name: first, command: echo one, depends_on: [missing]}\n",
            "tasks:\n"
            "  - {name: first, command: echo one}\n"
            "  - {name: first, command: echo two}\n",
            "tasks:\n"
            "  - {name: first, command: echo one, depends_on: [second]}\n"
            "  - {name: second, command: echo two, depends_on: [first]}\n",
            "tasks:\n"
            "  - {name: first, command: echo one, triggers: {on_success: [missing]}}\n",
            "tasks:\n"
            "  - {name: first, program: /bin/echo}\n",
            "tasks:\n"
            "  - {name: first, script: 'print(123)'}\n",
            "includes: {other: include.yml}\n"
            "tasks:\n"
            "  - {name: first, command: echo one}\n",
            "tasks: []\n"
        };
        const size_t count=sizeof sources/sizeof sources[0];
        size_t i;
        check(praktor_workflow_snapshot_create_inline(vstr_from_cstr(mixed_source),
            &saved,NULL,0)==SALTS_OK);
        out=saved;
        for(i=0;i<count;++i) {
            int status=praktor_workflow_snapshot_create_inline(
                vstr_from_cstr(sources[i]),&out,error,sizeof error);
            check(status==SALTS_EINVAL);
            check(out==saved);
            check(error[0]!='\0');
            check(praktor_workflow_snapshot_task_count(saved)==3u);
        }
        praktor_workflow_snapshot_destroy(saved);
    }
    it("desugars multi-command, regex and key/value task forms") {
        const char *sources[]={
            "tasks:\n"
            "  - name: multi\n"
            "    command: [echo a, echo b]\n"
            "    parse_regex: {pattern: '(.*)', output_key: found}\n",
            "tasks:\n"
            "  - name: kv\n"
            "    command: cat file\n"
            "    parse_keyvalue: {delimiter: '=', output_key: result}\n"
        };
        size_t i;
        for(i=0;i<sizeof sources/sizeof sources[0];++i) {
            praktor_workflow_snapshot *owner=NULL;
            const praktor_orch_node *root;
            char error[256]={0};
            check(praktor_workflow_snapshot_create_inline(vstr_from_cstr(sources[i]),
                &owner,error,sizeof error)==SALTS_OK);
            if(!owner)continue;
            root=praktor_orch_tree_root(praktor_workflow_snapshot_task_tree(owner,0u));
            check(root&&praktor_orch_node_type(root)==PRAKTOR_ORCH_SEQUENCE);
            check(praktor_orch_child_count(root)==(i==0?3u:2u));
            praktor_workflow_snapshot_destroy(owner);
        }
    }
}
