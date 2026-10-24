#include "yml/orch_yaml.h"
#include <tinytest.h>
#include <string.h>
static praktor_yaml_document *doc_for(const char *source){
    praktor_yaml_document *doc=NULL;
    check(praktor_yaml_document_create(vstr_from_cstr(source),&doc,NULL)==SALTS_OK);
    return doc;
}
spec("Named C11 orchestration YAML into canonical CSTL AST"){
    it("preserves ordered children and a nested decorator without borrowing input"){
        praktor_yaml_document *doc=doc_for(
            "Sequence:\n  - Shell: {cmd: echo first}\n  - Inverter:\n      child:\n        Shell: {cmd: echo second}\n");
        praktor_orch_tree *tree=NULL;
        const praktor_orch_node *first=NULL,*decorator=NULL,*second=NULL;
        praktor_orch_param p={0};
        char error[128]={0};
        check(praktor_orch_from_named_yaml(praktor_yaml_root(doc),&tree,error,sizeof error)==SALTS_OK);
        check(tree&&praktor_orch_tree_size(tree)==4&&!error[0]);
        praktor_yaml_document_destroy(doc);
        check(praktor_orch_child_at(praktor_orch_tree_root(tree),0,&first)==SALTS_OK);
        check(praktor_orch_node_type(first)==PRAKTOR_ORCH_SHELL);
        check(praktor_orch_param_at(first,0,&p)==SALTS_OK&&vstr_eq(p.value,vstr_from_cstr("echo first")));
        check(praktor_orch_child_at(praktor_orch_tree_root(tree),1,&decorator)==SALTS_OK);
        check(praktor_orch_node_type(decorator)==PRAKTOR_ORCH_INVERTER);
        check(praktor_orch_child_at(decorator,0,&second)==SALTS_OK);
        check(praktor_orch_param_at(second,0,&p)==SALTS_OK&&vstr_eq(p.value,vstr_from_cstr("echo second")));
        praktor_orch_tree_destroy(tree);
    }
    it("supports scalar shorthand and children form"){
        praktor_yaml_document *doc=doc_for(
            "Sequence:\n  children:\n    - Sleep: 25\n    - Shell: {cmd: echo, output_key: out}\n");
        praktor_orch_tree *tree=NULL;
        const praktor_orch_node *first=NULL;
        praktor_orch_param p={0};
        check(praktor_orch_from_named_yaml(praktor_yaml_root(doc),&tree,NULL,0)==SALTS_OK);
        check(tree&&praktor_orch_child_at(praktor_orch_tree_root(tree),0,&first)==SALTS_OK);
        check(praktor_orch_node_type(first)==PRAKTOR_ORCH_SLEEP);
        check(praktor_orch_param_at(first,0,&p)==SALTS_OK&&vstr_eq(p.value,vstr_from_cstr("25")));
        praktor_orch_tree_destroy(tree);praktor_yaml_document_destroy(doc);
    }
    it("rejects invalid syntax, duplicate params and child shapes without output changes"){
        const char *bad[]={
            "Sequence: []\n",
            "Shell: {cmd: echo, child: {Sleep: 1}}\n",
            "UnknownNode: x\n",
            "Shell: {cmd: first, CMD: second}\n",
            "Sequence: {child: {Shell: echo}, children: []}\n",
            "Sequence: {child: {Sleep: {duration: [1]}}}\n"
        };
        praktor_orch_command command={0};
        praktor_orch_tree *existing=NULL,*output;
        char error[128]={0};
        size_t i;
        command.command=vstr_from_cstr("unchanged");
        check(praktor_orch_from_command(&command,&existing,NULL,0)==SALTS_OK);
        output=existing;
        for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i){
            praktor_yaml_document *doc=doc_for(bad[i]);
            check(praktor_orch_from_named_yaml(praktor_yaml_root(doc),&output,error,sizeof error)!=SALTS_OK);
            check(output==existing&&error[0]!=0);
            praktor_yaml_document_destroy(doc);
        }
        praktor_orch_tree_destroy(existing);
    }
}
