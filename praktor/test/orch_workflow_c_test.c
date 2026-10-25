/* C11-only application-input qualification.  Reuses the existing canonical
 * workflow/task metadata parsers and orchestration AST; no second workflow
 * scheduler, C++ projection, filesystem access or dynamic SDK is involved. */
#include "yml/orch_yaml.h"
#include "yml/yaml_task_metadata.h"
#include "yml/yaml_workflow_metadata.h"
#include "yml/workflow_dependencies.h"
#include <json_parser.h>
#include <salts/error_codes.h>
#include <string.h>
#include <tinytest.h>

static bool json_text_is(const json_value_t *object,const char *key,const char *wanted) {
    const json_value_t *value=json_object_get(object,key);
    const char *text=value?json_string(value):NULL;
    return text&&strcmp(text,wanted)==0;
}
static bool node_has_text(const praktor_orch_node *node,const char *key,const char *wanted) {
    size_t i;
    for(i=0;i<praktor_orch_param_count(node);++i) {
        praktor_orch_param param={0};
        if(praktor_orch_param_at(node,i,&param)==SALTS_OK&&
           vstr_eq(param.key,vstr_from_cstr(key))&&
           vstr_eq(param.value,vstr_from_cstr(wanted)))return true;
    }
    return false;
}
static const char *workflow=
    "name: Native workflow\n"
    "variables: {MODE: check}\n"
    "tasks:\n"
    "  - name: prepare\n"
    "    command: echo ready\n"
    "  - name: orchestration\n"
    "    depends_on: [prepare]\n"
    "    actions:\n"
    "      Sequence:\n"
    "        - Shell: {cmd: echo ready}\n"
    "        - FileExists: {path: marker}\n"
    "  - name: choose\n"
    "    depends_on: [orchestration]\n"
    "    if: ready\n"
    "    then: [{Shell: {cmd: yes}}, {Sleep: 1}]\n"
    "    else: {Shell: {cmd: no}}\n"
    "  - name: parallel_task\n"
    "    depends_on: [choose, prepare]\n"
    "    parallel:\n"
    "      - Shell: {cmd: left}\n"
    "      - Shell: {cmd: right}\n";

spec("C11-only workflow metadata to orchestration AST") {
    it("retains every task and owned AST after the YAML input is destroyed") {
        praktor_yaml_document *doc=NULL;
        praktor_yaml_node task_nodes={0};
        json_value_t *root=NULL,*meta[4]={0};
        praktor_orch_tree *trees[4]={0};
        praktor_workflow_dependencies *graph=NULL;
        const json_value_t *views[4];
        char graph_error[256]={0};
        const char *names[]={"prepare","orchestration","choose","parallel_task"};
        const char *kinds[]={"command","actions","orch_root","orch_root"};
        praktor_orch_type expected_types[]={
            PRAKTOR_ORCH_TYPE_COUNT,PRAKTOR_ORCH_SEQUENCE,
            PRAKTOR_ORCH_IF_THEN_ELSE,PRAKTOR_ORCH_PARALLEL};
        size_t i;
        check(praktor_yaml_document_create(vstr_from_cstr(workflow),&doc,NULL)==SALTS_OK);
        check(praktor_yaml_workflow_metadata_create(praktor_yaml_root(doc),&root,NULL)==SALTS_OK);
        check(praktor_yaml_get(praktor_yaml_root(doc),"tasks",&task_nodes,NULL)==SALTS_OK);
        check(praktor_yaml_is_seq(task_nodes)&&praktor_yaml_count(task_nodes)==4u);
        for(i=0;i<4u;++i) {
            praktor_yaml_node task=praktor_yaml_at(task_nodes,i);
            char error[256]={0};
            int status;
            check(praktor_yaml_task_metadata_create(task,&meta[i],NULL)==SALTS_OK);
            if(i==0u)continue; /* Command lowering has its own C11 contract. */
            if(i==1u) {
                praktor_yaml_node branch={0};
                check(praktor_yaml_get(task,"actions",&branch,NULL)==SALTS_OK);
                status=praktor_orch_from_named_yaml(branch,&trees[i],error,sizeof error);
            } else
                status=praktor_orch_from_flat_yaml(task,i==2u?"if":"parallel",
                    &trees[i],error,sizeof error);
            if(status!=SALTS_OK)info("Task %u AST: %s",(unsigned)i,error);
            check(status==SALTS_OK);
        }
        praktor_yaml_document_destroy(doc);
        doc=NULL;
        check(json_text_is(root,"name","Native workflow"));
        for(i=0;i<4u;++i) {
            check(meta[i]&&json_text_is(meta[i],"name",names[i]));
            check(json_text_is(meta[i],"runner_kind",kinds[i]));
            if(i>0u) {
                const praktor_orch_node *node=praktor_orch_tree_root(trees[i]);
                check(node!=NULL);
                check(praktor_orch_node_type(node)==expected_types[i]);
            }
        }
        check(json_text_is(meta[1],"orch_node_type","actions")==false);
        check(node_has_text(praktor_orch_tree_root(trees[2]),"condition","ready"));
        check(praktor_orch_child_count(praktor_orch_tree_root(trees[2]))==2u);
        check(praktor_orch_child_count(praktor_orch_tree_root(trees[3]))==2u);
        {
            const json_value_t *deps=json_object_get(meta[3],"depends_on");
            check(deps&&json_array_size(deps)==2u);
            if(deps&&json_array_size(deps)==2u) {
                const char *first=json_string(json_array_get(deps,0u));
                const char *second=json_string(json_array_get(deps,1u));
                check(first&&strcmp(first,"choose")==0);
                check(second&&strcmp(second,"prepare")==0);
            }
        }
        for(i=0;i<4u;++i)views[i]=meta[i];
        check(praktor_workflow_dependencies_create(views,4u,&graph,
            graph_error,sizeof graph_error)==SALTS_OK);
        check(graph!=NULL&&graph_error[0]=='\0');
        for(i=0;i<4u;++i) {
            praktor_orch_tree_destroy(trees[i]);
            json_free(meta[i]);
        }
        json_free(root);
        /* The graph must outlive both source YAML and C-owned JSON metadata. */
        check(praktor_workflow_dependencies_task_count(graph)==4u);
        check(praktor_workflow_dependencies_edge_count(graph)==4u);
        for(i=0;i<4u;++i) {
            vstr name={0};
            check(praktor_workflow_dependencies_task_name(graph,i,&name)==SALTS_OK);
            check(vstr_eq(name,vstr_from_cstr(names[i])));
        }
        {
            size_t predecessor=SIZE_MAX,dependent=SIZE_MAX;
            const size_t expected[][2]={{0u,1u},{1u,2u},{2u,3u},{0u,3u}};
            for(i=0;i<4u;++i) {
                check(praktor_workflow_dependencies_edge_at(graph,i,
                    &predecessor,&dependent)==SALTS_OK);
                check(predecessor==expected[i][0]&&dependent==expected[i][1]);
            }
        }
        praktor_workflow_dependencies_destroy(graph);
    }
    it("preserves a qualified graph when duplicate names or cycles are rejected") {
        const char *bad_sources[]={
            "tasks:\n  - {name: same, command: echo first}\n"
            "  - {name: same, command: echo second}\n",
            "tasks:\n  - name: a\n    command: echo a\n    depends_on: [b]\n"
            "  - name: b\n    command: echo b\n    depends_on: [a]\n",
            "tasks:\n  - name: a\n    command: echo a\n    depends_on: [missing]\n",
            "tasks:\n  - name: a\n    command: echo a\n    triggers: {on_success: [missing]}\n"
        };
        const char *messages[]={
            "Duplicate task name: 'same'",
            "Cycle detected in workflow dependencies",
            "Task 'a' depends on unknown task 'missing'",
            "Task 'a' trigger 'on_success' references unknown task 'missing'"
        };
        praktor_workflow_dependencies *saved=NULL,*out;
        json_value_t *valid=json_create_object();
        const json_value_t *valid_items[1]={valid};
        size_t i;
        check(valid!=NULL);
        check(json_object_add_n(valid,"name",4u,json_create_string("retained")));
        check(praktor_workflow_dependencies_create(
            valid_items,1u,&saved,NULL,0)==SALTS_OK);
        out=saved;
        json_free(valid);
        for(i=0;i<sizeof(bad_sources)/sizeof(bad_sources[0]);++i) {
            praktor_yaml_document *doc=NULL;
            praktor_yaml_node tasks={0};
            json_value_t *items[2]={0};
            const json_value_t *borrowed[2]={0};
            char error[256]={0};
            size_t n;
            check(praktor_yaml_document_create(
                vstr_from_cstr(bad_sources[i]),&doc,NULL)==SALTS_OK);
            check(praktor_yaml_get(praktor_yaml_root(doc),"tasks",
                &tasks,NULL)==SALTS_OK);
            n=praktor_yaml_count(tasks);
            check(n>=1u&&n<=2u);
            for(size_t j=0;j<n;++j) {
                check(praktor_yaml_task_metadata_create(
                    praktor_yaml_at(tasks,j),&items[j],NULL)==SALTS_OK);
                borrowed[j]=items[j];
            }
            praktor_yaml_document_destroy(doc);
            check(praktor_workflow_dependencies_create(borrowed,n,
                &out,error,sizeof error)==SALTS_EINVAL);
            check(out==saved&&strcmp(error,messages[i])==0);
            for(size_t j=0;j<n;++j)json_free(items[j]);
        }
        {
            vstr text={0};
            check(praktor_workflow_dependencies_task_name(saved,0u,&text)==SALTS_OK);
            check(vstr_eq(text,vstr_from_cstr("retained")));
        }
        praktor_workflow_dependencies_destroy(saved);
    }

    it("preserves a qualified prior tree when a later task is malformed") {
        praktor_yaml_document *doc=NULL;
        praktor_yaml_node tasks={0};
        praktor_orch_tree *saved=NULL,*out;
        const praktor_orch_node *root;
        char error[256]={0};
        const char *source=
            "tasks:\n"
            "  - name: first\n"
            "    sequence: [{Shell: {cmd: good}}]\n"
            "  - name: second\n"
            "    sequence: [{Shell: {cmd: [not_scalar]}}]\n";
        check(praktor_yaml_document_create(vstr_from_cstr(source),&doc,NULL)==SALTS_OK);
        check(praktor_yaml_get(praktor_yaml_root(doc),"tasks",&tasks,NULL)==SALTS_OK);
        check(praktor_orch_from_flat_yaml(praktor_yaml_at(tasks,0),"sequence",
            &saved,error,sizeof error)==SALTS_OK);
        out=saved;
        check(praktor_orch_from_flat_yaml(praktor_yaml_at(tasks,1),"sequence",
            &out,error,sizeof error)==SALTS_EINVAL);
        check(out==saved);
        praktor_yaml_document_destroy(doc);
        root=praktor_orch_tree_root(saved);
        check(praktor_orch_node_type(root)==PRAKTOR_ORCH_SEQUENCE);
        check(praktor_orch_child_count(root)==1u);
        if(root&&praktor_orch_child_count(root)) {
            const praktor_orch_node *child=NULL;
            check(praktor_orch_child_at(root,0u,&child)==SALTS_OK);
            check(node_has_text(child,"cmd","good"));
        }
        praktor_orch_tree_destroy(saved);
    }
}
