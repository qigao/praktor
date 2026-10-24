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

    it("retains a map-style child sequence and last canonical parameter assignment"){
        praktor_yaml_document *doc=doc_for(
            "Sequence:\n"
            "  child:\n"
            "    - Shell: {CMD: first, c_m_d: second}\n"
            "    - Sleep: 2\n");
        praktor_orch_tree *tree=NULL;
        const praktor_orch_node *sequence=NULL,*shell=NULL,*sleep=NULL;
        check(praktor_orch_from_named_yaml(praktor_yaml_root(doc),&tree,NULL,0)==SALTS_OK);
        praktor_yaml_document_destroy(doc);
        check(tree&&praktor_orch_tree_size(tree)==4);
        check(praktor_orch_child_at(praktor_orch_tree_root(tree),0,&sequence)==SALTS_OK);
        check(praktor_orch_node_type(sequence)==PRAKTOR_ORCH_SEQUENCE&&praktor_orch_child_count(sequence)==2);
        check(praktor_orch_child_at(sequence,0,&shell)==SALTS_OK);
        check(praktor_orch_param_count(shell)==1&&has_param(shell,"cmd","second"));
        check(praktor_orch_child_at(sequence,1,&sleep)==SALTS_OK);
        check(praktor_orch_node_type(sleep)==PRAKTOR_ORCH_SLEEP);
        praktor_orch_tree_destroy(tree);
    }
    it("builds every flat single-child form using the canonical parameter key"){
        struct input {const char *kind,*yaml,*param,*value;praktor_orch_type type;} cases[]={
            {"timeout","name: x\ntimeout: 150\nchild: {Shell: {cmd: go}}\n","timeout_ms","150",PRAKTOR_ORCH_TIMEOUT},
            {"repeat","name: x\nrepeat: 3\nchild: {Shell: {cmd: go}}\n","num_cycles","3",PRAKTOR_ORCH_REPEAT},
            {"delay","name: x\ndelay: 25\nchild: {Shell: {cmd: go}}\n","delay_ms","25",PRAKTOR_ORCH_DELAY},
            {"keep_running_until_failure","name: x\nkeep_running_until_failure: 5\nchild: {Shell: {cmd: go}}\n","max_iterations","5",PRAKTOR_ORCH_KEEP_RUNNING_UNTIL_FAILURE},
            {"precondition","name: x\nprecondition: '{ready}'\nchild: {Shell: {cmd: go}}\n","condition","{ready}",PRAKTOR_ORCH_PRECONDITION},
            {"entry_updated","name: x\nentry_updated: value\nchild: {Shell: {cmd: go}}\n","watch_key","value",PRAKTOR_ORCH_ENTRY_UPDATED},
            {"consume_queue","name: x\nconsume_queue: queue\nchild: {Shell: {cmd: go}}\n","queue_key","queue",PRAKTOR_ORCH_CONSUME_QUEUE},
            {"run_once","name: x\nrun_once: ignored\nchild: {Shell: {cmd: go}}\n",NULL,NULL,PRAKTOR_ORCH_RUN_ONCE}
        };
        size_t i;
        for(i=0;i<sizeof(cases)/sizeof(cases[0]);++i){
            praktor_yaml_document *doc=doc_for(cases[i].yaml);
            praktor_orch_tree *tree=NULL;
            const praktor_orch_node *root,*child=NULL;
            char error[160]={0};
            check(praktor_orch_from_flat_yaml(praktor_yaml_root(doc),cases[i].kind,&tree,error,sizeof error)==SALTS_OK);
            praktor_yaml_document_destroy(doc);
            check(tree&&error[0]=='\0');
            if(!tree)continue;
            root=praktor_orch_tree_root(tree);
            check(praktor_orch_node_type(root)==cases[i].type);
            check(praktor_orch_param_count(root)==(cases[i].param?1u:0u));
            if(cases[i].param)check(has_param(root,cases[i].param,cases[i].value));
            check(praktor_orch_child_count(root)==1);
            check(praktor_orch_child_at(root,0,&child)==SALTS_OK);
            check(praktor_orch_node_type(child)==PRAKTOR_ORCH_SHELL&&has_param(child,"cmd","go"));
            praktor_orch_tree_destroy(tree);
        }
    }
    it("rejects retired named controls and malformed flat decorators"){
        const char *retired[]={
            "IfThenElse: {condition: ready, child: {Shell: {cmd: go}}}\n",
            "WhileDo: {condition: ready, child: {Shell: {cmd: go}}}\n",
            "Switch: {variable: key, children: [{Shell: {cmd: go}}]}\n",
            "SetVariable: {key: result, value: ready}\n",
            "Timeout: {timeout_ms: 1, child: {Shell: {cmd: go}}}\n",
            "Repeat: {num_cycles: 1, child: {Shell: {cmd: go}}}\n",
            "Delay: {delay_ms: 1, child: {Shell: {cmd: go}}}\n"
        };
        const struct {const char *kind,*yaml;} invalid[]={
            {"timeout","name: x\ntimeout: 1.5\nchild: {Shell: {cmd: go}}\n"},
            {"repeat","name: x\nrepeat: x\nchild: {Shell: {cmd: go}}\n"},
            {"run_once","name: x\nrun_once: yes\nchild: []\n"},
            {"precondition","name: x\nprecondition: [bad]\nchild: {Shell: {cmd: go}}\n"}
        };
        size_t i;
        for(i=0;i<sizeof(retired)/sizeof(retired[0]);++i){
            praktor_yaml_document *doc=doc_for(retired[i]);
            praktor_orch_tree *tree=NULL;
            char error[160]={0};
            check(praktor_orch_from_named_yaml(praktor_yaml_root(doc),&tree,error,sizeof error)==SALTS_EINVAL);
            check(!tree&&strstr(error,"Legacy orchestration")!=NULL);
            praktor_yaml_document_destroy(doc);
        }
        for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){
            praktor_yaml_document *doc=doc_for(invalid[i].yaml);
            praktor_orch_tree *tree=NULL;
            char error[160]={0};
            check(praktor_orch_from_flat_yaml(praktor_yaml_root(doc),invalid[i].kind,&tree,error,sizeof error)!=SALTS_OK);
            check(!tree&&error[0]);
            praktor_yaml_document_destroy(doc);
        }
    }

    it("validates integer and boolean node parameters while preserving runtime substitutions"){
        const char *bad[]={
            "ParseRegex: {input_key: input, pattern: x, capture_group: 1.5}\n",
            "Shell: {cmd: echo, stream_output: yes}\n",
            "FileExists: {path: file, fail_if_missing: 1}\n",
            "WaitEvent: {event: ready, timeout: --2}\n"
        };
        const char *good[]={
            "ParseRegex: {input_key: input, pattern: x, capture_group: -1}\n",
            "Shell: {cmd: echo, stream_output: TRUE}\n",
            "ParseRegex: {input_key: input, pattern: x, capture_group: '{group}' }\n"
        };
        size_t i;
        for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i){
            praktor_yaml_document *doc=doc_for(bad[i]);
            praktor_orch_tree *tree=NULL;char error[128]={0};
            check(praktor_orch_from_named_yaml(praktor_yaml_root(doc),&tree,error,sizeof error)!=SALTS_OK);
            check(!tree&&error[0]);
            praktor_yaml_document_destroy(doc);
        }
        for(i=0;i<sizeof(good)/sizeof(good[0]);++i){
            praktor_yaml_document *doc=doc_for(good[i]);
            praktor_orch_tree *tree=NULL;
            check(praktor_orch_from_named_yaml(praktor_yaml_root(doc),&tree,NULL,0)==SALTS_OK);
            check(tree!=NULL);
            praktor_orch_tree_destroy(tree);praktor_yaml_document_destroy(doc);
        }
    }

    it("rejects invalid syntax, duplicate params and child shapes without output changes"){
        const char *bad[]={
            "Sequence: []\n",
            "Shell: {cmd: echo, child: {Sleep: 1}}\n",
            "UnknownNode: x\n",
            "Shell: {cmd: echo, invalid: no}\n",
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

    it("projects task-level named runners without retaining the YAML document"){
        const struct {const char *runner,*source;praktor_orch_type type;} cases[]={
            {"sequence","name: task\nsequence: [{Shell: {cmd: hi}}, {Sleep: 1}]\n",PRAKTOR_ORCH_SEQUENCE},
            {"shell","name: task\nshell: {cmd: hello, output_key: output}\n",PRAKTOR_ORCH_SHELL},
            {"inverter","name: task\ninverter: {child: {Shell: {cmd: hi}}}\n",PRAKTOR_ORCH_INVERTER},
            {"run_once","name: task\nrun_once: {child: {Shell: {cmd: hi}}}\n",PRAKTOR_ORCH_RUN_ONCE},
            {"parse_regex","name: task\nparse_regex: {input_key: in, pattern: '(ok)'}\n",PRAKTOR_ORCH_PARSE_REGEX}
        };
        size_t i;
        for(i=0;i<sizeof(cases)/sizeof(cases[0]);++i){
            praktor_yaml_document *doc=doc_for(cases[i].source);
            praktor_orch_tree *tree=NULL;
            const praktor_orch_node *root;
            char error[160]={0};
            check(praktor_orch_from_flat_yaml(praktor_yaml_root(doc),cases[i].runner,
                &tree,error,sizeof error)==SALTS_OK);
            praktor_yaml_document_destroy(doc);
            check(tree&&error[0]=='\0');
            if(!tree)continue;
            root=praktor_orch_tree_root(tree);
            check(praktor_orch_node_type(root)==cases[i].type);
            check(praktor_orch_tree_size(tree)>0);
            praktor_orch_tree_destroy(tree);
        }
    }
    it("refuses malformed task-level named runners without changing existing output"){
        const char *source="name: bad\nshell: {cmd: hi, unexpected: value}\n";
        praktor_orch_command cmd={0};
        praktor_orch_tree *saved=NULL,*out;
        praktor_yaml_document *doc=doc_for(source);
        const praktor_orch_node *root;
        char error[160]={0};
        cmd.command=vstr_from_cstr("saved");
        check(praktor_orch_from_command(&cmd,&saved,NULL,0)==SALTS_OK);
        out=saved;
        check(praktor_orch_from_flat_yaml(praktor_yaml_root(doc),"shell",
            &out,error,sizeof error)==SALTS_EINVAL);
        check(out==saved&&strstr(error,"unexpected")!=NULL);
        praktor_yaml_document_destroy(doc);
        root=praktor_orch_tree_root(saved);
        check(praktor_orch_node_type(root)==PRAKTOR_ORCH_SEQUENCE);
        praktor_orch_tree_destroy(saved);
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
