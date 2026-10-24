#include "yml/orch_yaml.h"
#include <tinytest.h>
#include <string.h>
static praktor_yaml_document *doc_for(const char *source){
    praktor_yaml_document *doc=NULL;
    check(praktor_yaml_document_create(vstr_from_cstr(source),&doc,NULL)==SALTS_OK);
    return doc;
}

static bool has_param(const praktor_orch_node *node,const char *key,const char *value) {
    size_t i;
    for(i=0;i<praktor_orch_param_count(node);++i) {
        praktor_orch_param p={0};
        if(praktor_orch_param_at(node,i,&p)==SALTS_OK &&
           vstr_eq(p.key,vstr_from_cstr(key)) &&
           vstr_eq(p.value,vstr_from_cstr(value)))return true;
    }
    return false;
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

    it("builds flat if with scalar condition and ordered branches while ignoring task metadata"){
        praktor_yaml_document *doc=doc_for(
            "name: choose\nif: ready\nthen: {Shell: {cmd: run}}\nelse: {Shell: {cmd: skip}}\n");
        praktor_orch_tree *tree=NULL;
        const praktor_orch_node *root,*then_node=NULL,*else_node=NULL;
        char error[128]={0};
        check(praktor_orch_from_flat_yaml(praktor_yaml_root(doc),"if",&tree,error,sizeof error)==SALTS_OK);
        check(tree&&!error[0]);
        root=praktor_orch_tree_root(tree);
        check(praktor_orch_node_type(root)==PRAKTOR_ORCH_IF_THEN_ELSE);
        check(has_param(root,"condition","ready"));
        check(praktor_orch_child_count(root)==2);
        check(praktor_orch_child_at(root,0,&then_node)==SALTS_OK);
        check(praktor_orch_child_at(root,1,&else_node)==SALTS_OK);
        check(praktor_orch_node_type(then_node)==PRAKTOR_ORCH_SHELL&&has_param(then_node,"cmd","run"));
        check(praktor_orch_node_type(else_node)==PRAKTOR_ORCH_SHELL&&has_param(else_node,"cmd","skip"));
        praktor_orch_tree_destroy(tree);praktor_yaml_document_destroy(doc);
    }
    it("builds flat while with nested condition, max_iterations and do branch"){
        praktor_yaml_document *doc=doc_for(
            "name: loop\nwhile: {FileExists: {path: marker}}\nmax_iterations: 12\ndo: {Shell: {cmd: tick}}\n");
        praktor_orch_tree *tree=NULL;
        const praktor_orch_node *root,*condition=NULL,*body=NULL;
        check(praktor_orch_from_flat_yaml(praktor_yaml_root(doc),"while",&tree,NULL,0)==SALTS_OK);
        root=praktor_orch_tree_root(tree);
        check(praktor_orch_node_type(root)==PRAKTOR_ORCH_WHILE_DO);
        check(has_param(root,"max_iterations","12")&&praktor_orch_child_count(root)==2);
        check(praktor_orch_child_at(root,0,&condition)==SALTS_OK);
        check(praktor_orch_node_type(condition)==PRAKTOR_ORCH_FILE_EXISTS);
        check(praktor_orch_child_at(root,1,&body)==SALTS_OK);
        check(praktor_orch_node_type(body)==PRAKTOR_ORCH_SHELL&&has_param(body,"cmd","tick"));
        praktor_orch_tree_destroy(tree);praktor_yaml_document_destroy(doc);
    }
    it("builds switch cases and set_variable source/value forms"){
        praktor_yaml_document *doc=doc_for(
            "name: select\nswitch: mode\ncases:\n  - Shell: {cmd: one}\n  - Shell: {cmd: two}\n");
        praktor_orch_tree *tree=NULL;
        const praktor_orch_node *root=NULL,*first=NULL,*second=NULL;
        check(praktor_orch_from_flat_yaml(praktor_yaml_root(doc),"switch",&tree,NULL,0)==SALTS_OK);
        root=praktor_orch_tree_root(tree);
        check(praktor_orch_node_type(root)==PRAKTOR_ORCH_SWITCH&&has_param(root,"variable","mode"));
        check(praktor_orch_child_count(root)==2);
        check(praktor_orch_child_at(root,0,&first)==SALTS_OK&&has_param(first,"cmd","one"));
        check(praktor_orch_child_at(root,1,&second)==SALTS_OK&&has_param(second,"cmd","two"));
        praktor_orch_tree_destroy(tree);praktor_yaml_document_destroy(doc);
        doc=doc_for("name: assign\nset_variable: result\nfrom: source\n");
        tree=NULL;
        check(praktor_orch_from_flat_yaml(praktor_yaml_root(doc),"set_variable",&tree,NULL,0)==SALTS_OK);
        root=praktor_orch_tree_root(tree);
        check(praktor_orch_node_type(root)==PRAKTOR_ORCH_SET_VARIABLE);
        check(has_param(root,"key","result")&&has_param(root,"from","source"));
        praktor_orch_tree_destroy(tree);praktor_yaml_document_destroy(doc);
    }
    it("rejects malformed flat control forms atomically"){
        const char *bad[]={
            "name: x\nif: ready\n",
            "name: x\nwhile: ready\nmax_iterations: 1.5\ndo: {Shell: {cmd: tick}}\n",
            "name: x\nswitch: mode\ncases: {}\n",
            "name: x\nset_variable: result\nvalue: yes\nfrom: source\n",
            "name: x\nset_variable: result\nvalue: [not_scalar]\n",
            "name: x\nif: ready\nthen: {Shell: {cmd: run}}\nextra: bad\n"
        };
        praktor_orch_command command={0};
        praktor_orch_tree *previous=NULL,*out;
        char error[128]={0};
        size_t i;
        command.command=vstr_from_cstr("keep");
        check(praktor_orch_from_command(&command,&previous,NULL,0)==SALTS_OK);
        out=previous;
        for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i){
            praktor_yaml_document *doc=doc_for(bad[i]);
            check(praktor_orch_from_flat_yaml(praktor_yaml_root(doc),
                i==1?"while":i==2?"switch":i>=3&&i<=4?"set_variable":"if",
                &out,error,sizeof error)!=SALTS_OK);
            check(out==previous&&error[0]!=0);
            praktor_yaml_document_destroy(doc);
        }
        praktor_orch_tree_destroy(previous);
    }

}
