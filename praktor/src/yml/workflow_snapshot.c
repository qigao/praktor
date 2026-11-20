#include "yml/workflow_snapshot.h"
#include "yml/yaml_document.h"
#include "yml/yaml_task_metadata.h"
#include "yml/yaml_workflow_metadata.h"
#include "yml/yaml_action_params.h"
#include "yml/orch_yaml.h"
#include "actions/variable_reference.h"
#include <core/error_codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_LIBRARIES=16, MAX_COMPOSED_NAME=128, MAX_WORKFLOW_BYTES=1024*1024,
       MAX_ITEM_BYTES=4096 };
struct praktor_workflow_snapshot {
    json_value_t *metadata;
    json_value_t **tasks;
    praktor_orch_tree **trees;
    praktor_yaml_node *charts;
    praktor_yaml_node workflow_chart;
    praktor_yaml_document *chart_document;
    praktor_yaml_document *library_documents[MAX_LIBRARIES];
    praktor_workflow_snapshot_task_kind *kinds;
    praktor_workflow_dependencies *graph;
    size_t task_count;
};

static int snapshot_fail(char *error,size_t capacity,int status,
                         const char *message) {
    if(error&&capacity)(void)snprintf(error,capacity,"%s",message);
    return status;
}
static int field_failure(int status,praktor_yaml_field_error *field,
                         char *error,size_t capacity) {
    const char *message=field&&field->text?field->text:
        (status==SALTS_ENOMEM?"Failed to allocate workflow metadata":
         "Invalid native workflow metadata");
    snapshot_fail(error,capacity,status,message);
    praktor_yaml_field_error_dispose(field);
    return status;
}
void praktor_workflow_snapshot_destroy(praktor_workflow_snapshot *snapshot) {
    size_t i;
    if(!snapshot)return;
    for(i=0;i<snapshot->task_count;++i) {
        if(snapshot->trees)praktor_orch_tree_destroy(snapshot->trees[i]);
        if(snapshot->tasks)json_free(snapshot->tasks[i]);
    }
    praktor_workflow_dependencies_destroy(snapshot->graph);
    json_free(snapshot->metadata);
    free(snapshot->tasks);
    free(snapshot->trees);
    free(snapshot->charts);
    praktor_yaml_document_destroy(snapshot->chart_document);
    for(i=0;i<MAX_LIBRARIES;++i)praktor_yaml_document_destroy(snapshot->library_documents[i]);
    free(snapshot->kinds);
    free(snapshot);
}

/* Keep the existing YAML command constructor and AST desugaring as the only
 * interpretation of command postprocessors; this layer only transfers typed
 * borrowed views while their C action owner is still alive. */
static int command_tree(praktor_yaml_node task,praktor_orch_tree **out,
                        char *error,size_t capacity) {
    praktor_yaml_action_params *owner=NULL;
    praktor_yaml_command_view view={0};
    praktor_orch_command input={0};
    praktor_yaml_field_error field={0};
    vstr *commands=NULL;
    size_t i;
    int status=praktor_yaml_action_params_create(task,PRAKTOR_YAML_ACTION_COMMAND,
        &owner,&field);
    if(status!=SALTS_OK)return field_failure(status,&field,error,capacity);
    praktor_yaml_field_error_dispose(&field);
    status=praktor_yaml_action_command(owner,&view);
    if(status!=SALTS_OK) {
        status=snapshot_fail(error,capacity,status,"Cannot read C command parameters");
        goto done;
    }
    input.sequence=view.sequence;
    input.command=view.command;
    input.command_count=view.command_count;
    input.output_format=view.output_format;
    if(view.sequence&&view.command_count) {
        if(view.command_count>PRAKTOR_ORCH_MAX_COMMANDS) {
            status=snapshot_fail(error,capacity,SALTS_EINVAL,
                "Workflow command list exceeds native orchestration capacity");
            goto done;
        }
        commands=(vstr *)calloc(view.command_count,sizeof(*commands));
        if(!commands) {
            status=snapshot_fail(error,capacity,SALTS_ENOMEM,
                "Cannot allocate native command views");
            goto done;
        }
        for(i=0;i<view.command_count;++i) {
            status=praktor_yaml_action_argument(owner,i,&commands[i]);
            if(status!=SALTS_OK) {
                status=snapshot_fail(error,capacity,status,
                    "Cannot read native command entry");
                goto done;
            }
        }
        input.commands=commands;
    }
    switch(view.postprocessor) {
    case PRAKTOR_YAML_POST_REGEX:
        input.regex.present=true;
        input.regex.pattern=view.parser.regex.pattern;
        input.regex.output_key=view.parser.regex.output_key;
        input.regex.capture_group=view.parser.regex.capture_group;
        break;
    case PRAKTOR_YAML_POST_JSON:
        input.json.present=true;
        input.json.path=view.parser.json.path;
        input.json.output_key=view.parser.json.output_key;
        break;
    case PRAKTOR_YAML_POST_LINES:
        input.lines.present=true;
        input.lines.filter=view.parser.lines.filter;
        input.lines.output_key=view.parser.lines.output_key;
        break;
    case PRAKTOR_YAML_POST_KEYVALUE:
        input.keyvalue.present=true;
        input.keyvalue.delimiter=view.parser.keyvalue.delimiter;
        input.keyvalue.line_separator=view.parser.keyvalue.line_separator;
        input.keyvalue.output_key=view.parser.keyvalue.output_key;
        break;
    case PRAKTOR_YAML_POST_NONE:break;
    default:
        status=snapshot_fail(error,capacity,SALTS_EINVAL,
            "Unknown native command postprocessor");
        goto done;
    }
    status=praktor_orch_from_command(&input,out,error,capacity);
done:
    free(commands);
    praktor_yaml_action_params_destroy(owner);
    return status;
}

static int task_tree(praktor_yaml_node task,const json_value_t *metadata,
                     praktor_orch_tree **out,
                     praktor_workflow_snapshot_task_kind *kind,
                     char *error,size_t capacity) {
    const json_value_t *runner=json_object_get(metadata,"runner_kind");
    const json_value_t *count=json_object_get(metadata,"runner_count");
    const json_value_t *name=json_object_get(metadata,"name");
    const char *runner_name=runner?json_string(runner):NULL;
    const char *task_name=name?json_string(name):"<unknown>";
    if(count&&json_number(count)>1.0) {
        char message[256];
        (void)snprintf(message,sizeof message,
            "task '%s' declares multiple runners",task_name);
        return snapshot_fail(error,capacity,SALTS_EINVAL,message);
    }
    /* Scripts and other runtime-backed task kinds do not yet have C-owned
     * execution bodies here. Explicit refusal prevents a partial WorkflowPlan
     * from appearing qualified merely because a task has valid metadata. */
    if(praktor_yaml_has(task,"script"))
        return snapshot_fail(error,capacity,SALTS_EINVAL,
            "Unsupported C11 workflow snapshot script task");
    if(praktor_yaml_has(task,"args")||praktor_yaml_has(task,"stdin")) {
        if(error&&capacity)(void)snprintf(error,capacity,
            "task '%s': field '%s' is unsupported; declare process arguments in a Tool service",
            task_name,praktor_yaml_has(task,"args")?"args":"stdin");
        return SALTS_ENOTSUP;
    }
    if(!runner_name)
        return snapshot_fail(error,capacity,SALTS_EINVAL,
            praktor_yaml_has(task,"statechart")?
            "statechart requires a task runner such as actions":
            "Native workflow task must declare a supported runner");
    if(strcmp(runner_name,"command")==0) {
        *kind=PRAKTOR_WORKFLOW_SNAPSHOT_COMMAND;
        return command_tree(task,out,error,capacity);
    }
    if(praktor_yaml_has(task,"output_format"))
        return snapshot_fail(error,capacity,SALTS_ENOTSUP,
            "output_format requires the command runner");
    if(strcmp(runner_name,"tool")==0) {
        const char *service=json_get_string(metadata,"tool");
        if(!service||!*service)return snapshot_fail(error,capacity,SALTS_EINVAL,"tool must name a service");
        const json_value_t *inputs=json_object_get(metadata,"with");
        praktor_orch_input_param parameters[2]={
            {vstr_from_cstr("service"),vstr_from_cstr(service)}
        };
        char *payload=NULL;
        if(inputs) {
            size_t size=0u;
            payload=json_serialize(inputs,&size);
            if(!payload)return SALTS_ENOMEM;
            parameters[1]=(praktor_orch_input_param){
                vstr_from_cstr("with"),vstr_from_buf(payload,size)
            };
        }
        praktor_orch_flat_node node={PRAKTOR_ORCH_TOOL,PRAKTOR_ORCH_ROOT_PARENT,parameters,inputs?2u:1u};
        *kind=PRAKTOR_WORKFLOW_SNAPSHOT_ORCHESTRATION;
        const int rc=praktor_orch_from_flat_nodes(&node,1u,out,error,capacity);
        if(payload)json_serialize_free(payload);
        return rc;
    }
    if(strcmp(runner_name,"actions")==0) {
        praktor_yaml_node branch={0};
        int status=praktor_yaml_get(task,"actions",&branch,NULL);
        if(status!=SALTS_OK)
            return snapshot_fail(error,capacity,status,
                "Cannot read workflow actions node");
        *kind=PRAKTOR_WORKFLOW_SNAPSHOT_ORCHESTRATION;
        return praktor_orch_from_named_yaml(branch,out,error,capacity);
    }
    if(strcmp(runner_name,"orch_root")==0) {
        const json_value_t *selected=json_object_get(metadata,"orch_node_type");
        const char *node_name=selected?json_string(selected):NULL;
        if(!node_name)
            return snapshot_fail(error,capacity,SALTS_EINVAL,
                "Missing native workflow Orch runner type");
        *kind=PRAKTOR_WORKFLOW_SNAPSHOT_ORCHESTRATION;
        return praktor_orch_from_flat_yaml(task,node_name,out,error,capacity);
    }
    return snapshot_fail(error,capacity,SALTS_EINVAL,
        "Unsupported C11 workflow snapshot runner");
}

typedef struct task_library {
    tstr alias;
    praktor_yaml_node tasks;
    json_value_t **metadata;
    json_value_t *root;
    praktor_workflow_dependencies *graph;
    size_t count;
} task_library;

typedef struct snapshot_builder {
    praktor_workflow_snapshot *snapshot;
    task_library libraries[MAX_LIBRARIES];
    size_t library_count,limit,remaining_bytes;
    char *error;
    size_t capacity;
} snapshot_builder;

static int build_error(const snapshot_builder *b,int status,const char *message) {
    return snapshot_fail(b->error,b->capacity,status,message);
}

static void free_libraries(snapshot_builder *b) {
    for(size_t i=0u;i<b->library_count;++i) {
        task_library *lib=&b->libraries[i];
        for(size_t j=0u;j<lib->count&&lib->metadata;++j)
            json_free(lib->metadata[j]);
        free(lib->metadata);
        json_free(lib->root);
        praktor_workflow_dependencies_destroy(lib->graph);
        tstr_free(lib->alias);
    }
}

static int put_json(json_value_t *obj,const char *key,json_value_t *owned) {
    if(owned&&json_object_add_checked(obj,key,owned))return SALTS_OK;
    json_free(owned);
    return SALTS_ENOMEM;
}

static int push_json(json_value_t *array,json_value_t *owned) {
    if(owned&&json_array_add_checked(array,owned))return SALTS_OK;
    json_free(owned);
    return SALTS_ENOMEM;
}

static int overlay_vars(json_value_t *dst,const json_value_t *src) {
    for(size_t i=0u;i<json_object_size(src);++i) {
        json_value_t *item=json_clone(json_object_value(src,i));
        if(!item||!json_object_add_n(dst,json_object_key(src,i),json_object_key_len(src,i),item)) {
            json_free(item);
            return SALTS_ENOMEM;
        }
    }
    return SALTS_OK;
}

static int append_dependencies(json_value_t *dst,const json_value_t *src) {
    for(size_t i=0u;i<json_array_size(src);++i) {
        int rc=push_json(dst,json_clone(json_array_get(src,i)));
        if(rc!=SALTS_OK)return rc;
    }
    return SALTS_OK;
}

static int validate_variable_names(snapshot_builder *b,const json_value_t *metadata,const char *field) {
    const json_value_t *vars=json_object_get(metadata,field);
    for(size_t i=0u;i<json_object_size(vars);++i) {
        const vstr key={json_object_key(vars,i),json_object_key_len(vars,i)};
        if(key.len<=PRAKTOR_VARIABLE_MAX_KEY_BYTES&&praktor_valid_variable_name(key))continue;
        if(b->error&&b->capacity)(void)snprintf(b->error,b->capacity,
            "invalid variable name in %s: '%s'",field,key.data);
        return SALTS_EINVAL;
    }
    return SALTS_OK;
}

static int read_task_metadata(snapshot_builder *b,praktor_yaml_node task,json_value_t **out) {
    praktor_yaml_field_error field={0};
    int rc=praktor_yaml_task_metadata_create(task,out,&field);
    if(rc!=SALTS_OK)return field_failure(rc,&field,b->error,b->capacity);
    praktor_yaml_field_error_dispose(&field);
    rc=validate_variable_names(b,*out,"vars");
    if(rc!=SALTS_OK) {
        json_free(*out);
        *out=NULL;
    }
    return rc;
}

static int composed_keys(snapshot_builder *b,praktor_yaml_node node,
                         const vstr *keys,size_t count) {
    praktor_yaml_field_error field={0};
    int rc=praktor_yaml_check_keys(node,keys,count,&field);
    if(rc!=SALTS_OK)return field_failure(rc,&field,b->error,b->capacity);
    praktor_yaml_field_error_dispose(&field);
    return SALTS_OK;
}

static int name_segment(const char *text,size_t length) {
    if(!length||length>MAX_COMPOSED_NAME)return 0;
    for(size_t i=0u;i<length;++i) {
        const char ch=text[i];
        if(!praktor_identifier_start(ch)&&!(ch>='0'&&ch<='9')&&ch!='-')return 0;
    }
    return 1;
}

static int read_library_document(snapshot_builder *b,const char *path,praktor_yaml_document **out) {
    FILE *file=fopen(path,"rb");
    if(!file) {
        if(b->error&&b->capacity)(void)snprintf(b->error,b->capacity,"cannot open task library '%s'",path);
        return SALTS_EINVAL;
    }
    const long size=fseek(file,0,SEEK_END)==0?ftell(file):-1;
    if(size<=0||(size_t)size>b->remaining_bytes||fseek(file,0,SEEK_SET)!=0) {
        fclose(file);
        return build_error(b,SALTS_EINVAL,"task library exceeds the YAML byte budget or cannot be read");
    }
    char *bytes=malloc((size_t)size);
    if(!bytes) { fclose(file);return SALTS_ENOMEM; }
    const size_t read=fread(bytes,1u,(size_t)size,file);
    fclose(file);
    praktor_yaml_diagnostic diagnostic={b->error,b->capacity,PRAKTOR_YAML_FAILURE_NONE};
    const int rc=read==(size_t)size?praktor_yaml_document_create(vstr_from_buf(bytes,read),out,&diagnostic):
        build_error(b,SALTS_EINVAL,"cannot read complete task library");
    free(bytes);
    if(rc==SALTS_OK)b->remaining_bytes-=read;
    return rc;
}

static bool template_runner(const json_value_t *metadata) {
    const char *kind=json_get_string(metadata,"runner_kind");
    return kind&&json_number(json_object_get(metadata,"runner_count"))==1&&
        (!strcmp(kind,"command")||!strcmp(kind,"actions")||
         !strcmp(kind,"orch_root")||!strcmp(kind,"tool"));
}

static int load_library(snapshot_builder *b,praktor_yaml_node entry) {
    const size_t index=b->library_count++;
    task_library *lib=&b->libraries[index];
    tstr path=NULL;
    int rc=praktor_yaml_text(entry,true,&lib->alias);
    if(rc==SALTS_OK)rc=praktor_yaml_text(entry,false,&path);
    if(rc==SALTS_OK) {
        if(!name_segment(lib->alias,tstr_len(lib->alias))||!tstr_len(path)||
           strlen(path)!=tstr_len(path))
            rc=build_error(b,SALTS_EINVAL,"includes requires a library alias and literal file path");
        else rc=read_library_document(b,path,&b->snapshot->library_documents[index]);
    }else if(rc!=SALTS_ENOMEM)
        rc=build_error(b,rc,"includes requires a library alias and literal file path");
    tstr_free(path);
    if(rc!=SALTS_OK)return rc;

    static const vstr root_keys[]={{"name",4},{"description",11},{"variables",9},{"tasks",5}};
    const praktor_yaml_node root=praktor_yaml_root(b->snapshot->library_documents[index]);
    if(!praktor_yaml_is_map(root))return build_error(b,SALTS_EINVAL,"task library must be a map");
    rc=composed_keys(b,root,root_keys,sizeof root_keys/sizeof *root_keys);
    if(rc!=SALTS_OK)return rc;
    praktor_yaml_field_error field={0};
    rc=praktor_yaml_workflow_metadata_create(root,&lib->root,&field);
    if(rc!=SALTS_OK)return field_failure(rc,&field,b->error,b->capacity);
    rc=validate_variable_names(b,lib->root,"variables");
    if(rc!=SALTS_OK)return rc;
    rc=praktor_yaml_get(root,"tasks",&lib->tasks,NULL);
    if(rc!=SALTS_OK||!praktor_yaml_is_seq(lib->tasks))
        return build_error(b,rc==SALTS_ENOMEM?rc:SALTS_EINVAL,"task library requires a tasks sequence");
    lib->count=praktor_yaml_count(lib->tasks);
    if(!lib->count||lib->count>PRAKTOR_WORKFLOW_MAX_TASKS)
        return build_error(b,SALTS_EINVAL,"invalid task library task count");
    lib->metadata=calloc(lib->count,sizeof(*lib->metadata));
    if(!lib->metadata)return SALTS_ENOMEM;
    const json_value_t **views=malloc(lib->count*sizeof(*views));
    if(!views)return SALTS_ENOMEM;
    for(size_t i=0u;i<lib->count;++i) {
        const praktor_yaml_node task=praktor_yaml_at(lib->tasks,i);
        rc=read_task_metadata(b,task,&lib->metadata[i]);
        if(rc!=SALTS_OK)break;
        views[i]=lib->metadata[i];
        const json_value_t *name=json_object_get(views[i],"name");
        if(praktor_yaml_has(task,"script")||!template_runner(views[i])||
           !name_segment(json_string(name),json_string_len(name))) {
            rc=build_error(b,SALTS_EINVAL,"library tasks require a simple name and one ordinary runner");
            break;
        }
    }
    if(rc==SALTS_OK)rc=praktor_workflow_dependencies_create(views,lib->count,&lib->graph,
                                                            b->error,b->capacity);
    free(views);
    return rc;
}

static int load_libraries(snapshot_builder *b,praktor_yaml_node root) {
    if(!praktor_yaml_has(root,"includes"))return SALTS_OK;
    praktor_yaml_node includes={0};
    int rc=praktor_yaml_get(root,"includes",&includes,NULL);
    if(rc!=SALTS_OK)return rc;
    if(!praktor_yaml_is_map(includes)||praktor_yaml_count(includes)>MAX_LIBRARIES)
        return build_error(b,SALTS_EINVAL,"includes must map at most 16 aliases to task libraries");
    for(size_t i=0u;i<praktor_yaml_count(includes);++i) {
        rc=load_library(b,praktor_yaml_at(includes,i));
        if(rc!=SALTS_OK)return rc;
    }
    return SALTS_OK;
}

/* append_task consumes metadata even when tree admission or chart binding fails. */
static int append_task(snapshot_builder *b,praktor_yaml_node task,
    praktor_yaml_node chart,json_value_t *metadata,bool join) {
    praktor_workflow_snapshot *s=b->snapshot;
    if(s->task_count==b->limit) {
        json_free(metadata);
        return build_error(b,SALTS_EINVAL,"workflow exceeds configured task capacity after expansion");
    }
    const size_t index=s->task_count++;
    s->tasks[index]=metadata;
    int rc;
    if(join) {
        static const praktor_orch_input_param params[]={
            {{"condition",9},{"true",4}},{{"guard_fields",12},{"",0}},
            {{"guard_kind",10},{"expr",4}}
        };
        const praktor_orch_flat_node node={PRAKTOR_ORCH_ASSERT,PRAKTOR_ORCH_ROOT_PARENT,
                                          params,sizeof params/sizeof *params};
        s->kinds[index]=PRAKTOR_WORKFLOW_SNAPSHOT_ORCHESTRATION;
        rc=praktor_orch_from_flat_nodes(&node,1u,&s->trees[index],b->error,b->capacity);
    }else rc=task_tree(task,metadata,&s->trees[index],&s->kinds[index],b->error,b->capacity);
    if(rc!=SALTS_OK)return rc;
    if(!praktor_yaml_has(chart,"statechart"))return SALTS_OK;
    if(!s->charts)s->charts=calloc(b->limit,sizeof(*s->charts));
    if(!s->charts)return SALTS_ENOMEM;
    return praktor_yaml_get(chart,"statechart",&s->charts[index],NULL);
}

static int composed_metadata(const json_value_t *source,const json_value_t *caller,
    const json_value_t *library,const char *name,json_value_t *dependencies,
    const char *item,const char *index,bool entry,json_value_t **out) {
    json_value_t *copy=json_clone(source);
    json_value_t *vars=json_create_object();
    int rc=copy&&vars?SALTS_OK:SALTS_ENOMEM;
    if(rc==SALTS_OK)rc=overlay_vars(vars,json_object_get(library,"variables"));
    if(rc==SALTS_OK)rc=overlay_vars(vars,json_object_get(source,"vars"));
    if(rc==SALTS_OK)rc=overlay_vars(vars,json_object_get(caller,"vars"));
    if(rc==SALTS_OK&&item)rc=put_json(vars,"item",json_create_string(item));
    if(rc==SALTS_OK&&index)rc=put_json(vars,"index",json_create_string(index));
    if(rc==SALTS_OK) {
        rc=put_json(copy,"vars",vars);
        vars=NULL;
    }
    if(rc==SALTS_OK)rc=put_json(copy,"name",json_create_string(name));
    if(rc==SALTS_OK) {
        rc=put_json(copy,"depends_on",dependencies);
        dependencies=NULL;
    }
    const json_value_t *outputs=entry?json_object_get(caller,"outputs"):NULL;
    if(rc==SALTS_OK&&outputs)rc=put_json(copy,"outputs",json_clone(outputs));
    const json_value_t *description=entry?json_object_get(caller,"description"):NULL;
    if(rc==SALTS_OK&&description)
        rc=put_json(copy,"description",json_clone(description));
    json_free(vars);
    json_free(dependencies);
    if(rc==SALTS_OK)*out=copy;
    else json_free(copy);
    return rc;
}

static int scoped_name(char *out,const char *scope,const char *name) {
    const int length=snprintf(out,MAX_COMPOSED_NAME+1u,"%s.%s",scope,name);
    return length>0&&length<=MAX_COMPOSED_NAME?SALTS_OK:SALTS_EINVAL;
}

static int instantiate_library(snapshot_builder *b,praktor_yaml_node invocation,
    const json_value_t *caller) {
    tstr reference=NULL;
    praktor_yaml_node uses={0};
    int rc=praktor_yaml_get(invocation,"uses",&uses,NULL);
    if(rc==SALTS_OK)rc=praktor_yaml_text(uses,false,&reference);
    if(rc!=SALTS_OK) {
        tstr_free(reference);
        return build_error(b,rc,"uses requires a library.task reference");
    }
    const char *dot=strchr(reference,'.');
    const size_t alias=dot?(size_t)(dot-reference):0u;
    task_library *lib=NULL;
    if(dot&&strlen(reference)==tstr_len(reference)) {
        for(size_t i=0u;i<b->library_count;++i)
            if(alias==tstr_len(b->libraries[i].alias)&&!memcmp(reference,b->libraries[i].alias,alias)) {
                lib=&b->libraries[i];
                break;
            }
    }
    size_t entry=0u;
    if(lib)
        while(entry<lib->count&&strcmp(dot+1u,json_get_string(lib->metadata[entry],"name")))++entry;
    if(!lib||entry==lib->count) {
        if(b->error&&b->capacity)(void)snprintf(b->error,b->capacity,
            "uses references unknown library task '%s'",reference);
        tstr_free(reference);
        return SALTS_EINVAL;
    }
    tstr_free(reference);
    const char *scope=json_get_string(caller,"name");
    if(!name_segment(scope,strlen(scope)))
        return build_error(b,SALTS_EINVAL,"uses requires a simple task name");

    bool needed[PRAKTOR_WORKFLOW_MAX_TASKS]={0};
    needed[entry]=true;
    size_t count=1u;
    const size_t available=b->limit-b->snapshot->task_count;
    if(count>available)
        return build_error(b,SALTS_EINVAL,"uses exceeds configured task capacity after expansion");
    const size_t edge_count=praktor_workflow_dependencies_edge_count(lib->graph);
    bool changed;
    do {
        changed=false;
        for(size_t i=0u;i<edge_count;++i) {
            size_t from,to;
            rc=praktor_workflow_dependencies_edge_at(lib->graph,i,&from,&to);
            if(rc!=SALTS_OK)return rc;
            if(needed[to]&&!needed[from]) {
                if(++count>available)
                    return build_error(b,SALTS_EINVAL,"uses exceeds configured task capacity after expansion");
                needed[from]=changed=true;
            }
        }
    }while(changed);
    /* Preserve the call site's position for the entry and the library's order
     * for its prerequisites. The graph owns their actual execution order. */
    for(size_t n=0u;n<=lib->count;++n) {
        const size_t i=n==0u?entry:n-1u;
        if((n&&i==entry)||!needed[i])continue;
        const json_value_t *source=lib->metadata[i];
        const char *name=scope;
        char scoped[MAX_COMPOSED_NAME+1u];
        if(i!=entry) {
            if(scoped_name(scoped,scope,json_get_string(source,"name"))!=SALTS_OK)
                return build_error(b,SALTS_EINVAL,"library task name exceeds composition capacity");
            name=scoped;
        }
        json_value_t *deps=json_create_array();
        if(!deps)return SALTS_ENOMEM;
        rc=append_dependencies(deps,json_object_get(caller,"depends_on"));
        const json_value_t *local=json_object_get(source,"depends_on");
        for(size_t j=0u;rc==SALTS_OK&&j<json_array_size(local);++j) {
            char target[MAX_COMPOSED_NAME+1u];
            rc=scoped_name(target,scope,json_string(json_array_get(local,j)));
            if(rc==SALTS_OK)rc=push_json(deps,json_create_string(target));
            else rc=build_error(b,rc,"library dependency name exceeds composition capacity");
        }
        if(rc!=SALTS_OK) { json_free(deps);return rc; }
        json_value_t *metadata=NULL;
        rc=composed_metadata(source,caller,lib->root,name,deps,NULL,NULL,i==entry,&metadata);
        if(rc!=SALTS_OK)return rc;
        const praktor_yaml_node task=praktor_yaml_at(lib->tasks,i);
        const praktor_yaml_node chart=i==entry&&praktor_yaml_has(invocation,"statechart")?invocation:task;
        rc=append_task(b,task,chart,metadata,false);
        if(rc!=SALTS_OK)return rc;
    }
    return SALTS_OK;
}

static int dynamic_name(const char *pattern,const char *item,const char *index,char *out) {
    size_t written=0u;
    while(*pattern) {
        const char *value=pattern;
        size_t length=1u;
        if(*pattern=='$') {
            ++pattern;
            size_t span=0u;
            while(praktor_identifier_start(pattern[span])||
                  (pattern[span]>='0'&&pattern[span]<='9'))++span;
            if(span==4u&&!memcmp(pattern,"item",4u))value=item;
            else if(span==5u&&!memcmp(pattern,"index",5u))value=index;
            else return SALTS_EINVAL;
            length=strlen(value);
            pattern+=span;
        }else ++pattern;
        if(!name_segment(value,length)||length>MAX_COMPOSED_NAME-written)return SALTS_EINVAL;
        memcpy(out+written,value,length);
        written+=length;
    }
    out[written]='\0';
    return written?SALTS_OK:SALTS_EINVAL;
}

static int dynamic_items(snapshot_builder *b,praktor_yaml_node variable,
    const json_value_t *caller,json_value_t **out) {
    tstr ref=NULL;
    int rc=praktor_yaml_text(variable,false,&ref);
    if(rc!=SALTS_OK) {
        tstr_free(ref);
        return build_error(b,rc,"items_variable requires a $name reference");
    }
    const vstr name=praktor_variable_reference(vstr_from_buf(ref,tstr_len(ref)));
    const json_value_t *value=NULL;
    const char *text=NULL;
    size_t length=0u;
    if(name.len) {
        value=json_object_get(json_object_get(caller,"vars"),name.data);
        if(!value)value=json_object_get(json_object_get(b->snapshot->metadata,"variables"),name.data);
        text=value?json_string(value):getenv(name.data);
        if(text)length=value?json_string_len(value):strlen(text);
    }
    if(text&&length&&length<=MAX_ITEM_BYTES&&!memchr(text,0,length))
        *out=json_parse(text,length);
    tstr_free(ref);
    if(!*out||json_type(*out)!=JSON_ARRAY)
        return build_error(b,SALTS_EINVAL,"items_variable must resolve to a JSON array of at most 4096 bytes");
    return SALTS_OK;
}

static int instantiate_dynamic(snapshot_builder *b,praktor_yaml_node node,json_value_t *caller) {
    static const vstr keys[]={{"items_variable",14},{"template",8},{"collect",7}};
    praktor_yaml_node section={0},variable={0},task={0};
    json_value_t *items=NULL,*base=NULL,*join_deps=NULL;
    tstr collect=NULL;
    const size_t first=b->snapshot->task_count;
    int rc=praktor_yaml_get(node,"dynamic_tasks",&section,NULL);
    if(rc!=SALTS_OK)goto done;
    if(!praktor_yaml_is_map(section)) {
        rc=build_error(b,SALTS_EINVAL,"dynamic_tasks must be a map");
        goto done;
    }
    rc=composed_keys(b,section,keys,sizeof keys/sizeof *keys);
    if(rc!=SALTS_OK)goto done;
    rc=praktor_yaml_get(section,"items_variable",&variable,NULL);
    if(rc==SALTS_OK)rc=praktor_yaml_get(section,"template",&task,NULL);
    if(rc!=SALTS_OK) {
        if(rc==SALTS_ENOENT)
            rc=build_error(b,SALTS_EINVAL,"dynamic_tasks requires items_variable and template");
        goto done;
    }
    rc=read_task_metadata(b,task,&base);
    if(rc!=SALTS_OK)goto done;
    if(!template_runner(base)||praktor_yaml_has(task,"script")) {
        rc=build_error(b,SALTS_EINVAL,"dynamic task template requires one ordinary runner");
        goto done;
    }
    if(praktor_yaml_has(section,"collect")) {
        praktor_yaml_node target={0};
        rc=praktor_yaml_get(section,"collect",&target,NULL);
        if(rc==SALTS_OK)rc=praktor_yaml_text(target,false,&collect);
        if(rc!=SALTS_OK) {
            if(rc!=SALTS_ENOMEM)
                rc=build_error(b,rc,"dynamic_tasks collect requires a writable $name");
            goto done;
        }
        const vstr name=praktor_variable_reference(vstr_from_buf(collect,tstr_len(collect)));
        if(!name.len||name.len>PRAKTOR_VARIABLE_MAX_KEY_BYTES||
           !strncmp(name.data,"chart.",6u)||!strncmp(name.data,"event.",6u)) {
            rc=build_error(b,SALTS_EINVAL,"dynamic_tasks collect requires a writable $name");
            goto done;
        }
    }
    const char *scope=json_get_string(caller,"name"),*pattern=json_get_string(base,"name");
    char rendered[MAX_COMPOSED_NAME+1u],name[MAX_COMPOSED_NAME+1u];
    if(!name_segment(scope,strlen(scope))||dynamic_name(pattern,"x","0",rendered)!=SALTS_OK||
       scoped_name(name,scope,rendered)!=SALTS_OK) {
        rc=build_error(b,SALTS_EINVAL,"invalid dynamic task name or template name");
        goto done;
    }
    rc=dynamic_items(b,variable,caller,&items);
    if(rc!=SALTS_OK)goto done;
    if(!json_array_size(items)) {
        praktor_orch_tree *tree=NULL;
        praktor_workflow_snapshot_task_kind kind;
        rc=task_tree(task,base,&tree,&kind,b->error,b->capacity);
        praktor_orch_tree_destroy(tree);
        if(rc!=SALTS_OK)goto done;
    }
    if(json_array_size(items)>=b->limit-b->snapshot->task_count) {
        rc=build_error(b,SALTS_EINVAL,"dynamic tasks exceed configured task capacity after expansion");
        goto done;
    }
    join_deps=json_create_array();
    if(!join_deps) { rc=SALTS_ENOMEM;goto done; }
    rc=append_dependencies(join_deps,json_object_get(caller,"depends_on"));
    for(size_t i=0u;rc==SALTS_OK&&i<json_array_size(items);++i) {
        const json_value_t *element=json_array_get(items,i);
        char ordinal[24],*encoded=NULL;
        const char *item=json_string(element);
        size_t length=json_string_len(element);
        if(!item) {
            encoded=json_serialize(element,&length);
            item=encoded;
        }
        (void)snprintf(ordinal,sizeof ordinal,"%zu",i);
        if(!item)rc=SALTS_ENOMEM;
        else if(length>MAX_ITEM_BYTES||memchr(item,0,length)||
                dynamic_name(pattern,item,ordinal,rendered)!=SALTS_OK||
                scoped_name(name,scope,rendered)!=SALTS_OK)
            rc=build_error(b,SALTS_EINVAL,"invalid dynamic task item or generated name");
        else {
            json_value_t *deps=json_create_array(),*metadata=NULL;
            rc=deps?append_dependencies(deps,json_object_get(caller,"depends_on")):SALTS_ENOMEM;
            if(rc==SALTS_OK)rc=append_dependencies(deps,json_object_get(base,"depends_on"));
            if(rc==SALTS_OK)
                rc=composed_metadata(base,caller,NULL,name,deps,item,ordinal,false,&metadata);
            else json_free(deps);
            if(rc==SALTS_OK&&collect) {
                rc=put_json(metadata,"collect_member",json_create_bool(true));
                if(rc!=SALTS_OK)json_free(metadata);
            }
            if(rc==SALTS_OK)rc=append_task(b,task,task,metadata,false);
            if(rc==SALTS_OK)rc=push_json(join_deps,json_create_string(name));
        }
        json_serialize_free(encoded);
    }
    if(rc==SALTS_OK&&collect) {
        const json_value_t *specified=json_object_get(caller,"outputs");
        json_value_t *outputs=specified?json_clone(specified):json_create_array();
        if(!outputs) { rc=SALTS_ENOMEM;goto done; }
        bool listed=false;
        for(size_t i=0u;i<json_array_size(outputs);++i)
            if(!strcmp(json_string(json_array_get(outputs,i)),collect))listed=true;
        if(!listed)rc=push_json(outputs,json_create_string(collect));
        if(rc==SALTS_OK)rc=put_json(caller,"outputs",outputs);
        else json_free(outputs);
        if(rc==SALTS_OK)rc=put_json(caller,"collect_key",json_create_string(collect+1u));
        if(rc==SALTS_OK)rc=put_json(caller,"collect_first",json_create_number((double)first));
        if(rc==SALTS_OK)rc=put_json(caller,"collect_count",json_create_number((double)json_array_size(items)));
    }
    if(rc==SALTS_OK) {
        rc=put_json(caller,"depends_on",join_deps);
        join_deps=NULL;
    }
    if(rc==SALTS_OK) {
        rc=append_task(b,node,node,caller,true);
        caller=NULL;
    }
done:
    tstr_free(collect);
    json_free(join_deps);
    json_free(caller);
    json_free(base);
    json_free(items);
    return rc;
}

static int admit_task(snapshot_builder *b,praktor_yaml_node node,bool compose) {
    json_value_t *metadata=NULL;
    int rc=read_task_metadata(b,node,&metadata);
    if(rc!=SALTS_OK)return rc;
    const bool uses=compose&&praktor_yaml_has(node,"uses");
    const bool dynamic=compose&&praktor_yaml_has(node,"dynamic_tasks");
    if(!uses&&!dynamic)return append_task(b,node,node,metadata,false);
    const vstr keys[]={{"name",4},{"description",11},{"vars",4},{"depends_on",10},
                       {"outputs",7},{"statechart",10},
                       uses?(vstr){"uses",4}:(vstr){"dynamic_tasks",13}};
    rc=composed_keys(b,node,keys,sizeof keys/sizeof *keys);
    if(rc==SALTS_OK&&dynamic)return instantiate_dynamic(b,node,metadata);
    if(rc==SALTS_OK)rc=instantiate_library(b,node,metadata);
    json_free(metadata);
    return rc;
}

static int create_snapshot(vstr yaml_source,size_t limit,bool compose,
    praktor_workflow_snapshot **out,char *error,size_t error_capacity) {
    praktor_yaml_document *document=NULL;
    praktor_yaml_node root,tasks={0};
    praktor_workflow_snapshot *snapshot=NULL;
    praktor_yaml_field_error field={0};
    praktor_yaml_diagnostic yaml_error={error,error_capacity,PRAKTOR_YAML_FAILURE_NONE};
    const json_value_t **views=NULL;
    size_t count=0;
    snapshot_builder builder={.limit=limit,.error=error,.capacity=error_capacity};
    int status;
    if(error&&error_capacity)error[0]='\0';
    if(!out||(!error&&error_capacity)||!vstr_is_valid(yaml_source)||
       yaml_source.len==0||limit==0u||limit>PRAKTOR_WORKFLOW_MAX_TASKS||
       (compose&&yaml_source.len>MAX_WORKFLOW_BYTES))
        return snapshot_fail(error,error_capacity,SALTS_EINVAL,
            "Invalid inline workflow source or output");
    status=praktor_yaml_document_create(yaml_source,&document,&yaml_error);
    if(status!=SALTS_OK)goto done;
    root=praktor_yaml_root(document);
    if(!praktor_yaml_is_map(root)) {
        status=snapshot_fail(error,error_capacity,SALTS_EINVAL,
            "workflow must be a map");
        goto done;
    }
    if(!compose&&praktor_yaml_has(root,"includes")) {
        status=snapshot_fail(error,error_capacity,SALTS_EINVAL,
            "inline workflows cannot declare includes");
        goto done;
    }
    if(praktor_yaml_has(root,"env")) {
        status=snapshot_fail(error,error_capacity,SALTS_ENOTSUP,
            "workflow field 'env' is unsupported; use dotEnv or the inherited environment");
        goto done;
    }
    if(praktor_yaml_has(root,"outputs")) {
        status=snapshot_fail(error,error_capacity,SALTS_ENOTSUP,
            "workflow field 'outputs' is unsupported; direct tasks publish their own outputs");
        goto done;
    }
    status=praktor_yaml_get(root,"tasks",&tasks,NULL);
    if(status!=SALTS_OK) {
        status=snapshot_fail(error,error_capacity,
            status==SALTS_ENOENT?SALTS_EINVAL:status,
            "workflow must define a 'tasks' list");
        goto done;
    }
    if(!praktor_yaml_is_seq(tasks)) {
        status=snapshot_fail(error,error_capacity,SALTS_EINVAL,
            "'tasks' must be a sequence");
        goto done;
    }
    count=praktor_yaml_count(tasks);
    if(!count||count>limit) {
        status=snapshot_fail(error,error_capacity,SALTS_EINVAL,
            "Invalid native workflow task count or capacity");
        goto done;
    }
    snapshot=(praktor_workflow_snapshot *)calloc(1,sizeof(*snapshot));
    if(!snapshot) {
        status=snapshot_fail(error,error_capacity,SALTS_ENOMEM,
            "Cannot allocate native workflow snapshot");
        goto done;
    }
    builder.snapshot=snapshot;
    builder.limit=compose?limit:count;
    snapshot->tasks=(json_value_t **)calloc(builder.limit,sizeof(*snapshot->tasks));
    snapshot->trees=(praktor_orch_tree **)calloc(builder.limit,sizeof(*snapshot->trees));
    snapshot->kinds=(praktor_workflow_snapshot_task_kind *)calloc(builder.limit,
        sizeof(*snapshot->kinds));
    if(!snapshot->tasks||!snapshot->trees||!snapshot->kinds) {
        status=snapshot_fail(error,error_capacity,SALTS_ENOMEM,
            "Cannot allocate native workflow task owners");
        goto done;
    }
    status=praktor_yaml_workflow_metadata_create(root,&snapshot->metadata,&field);
    if(status!=SALTS_OK) {
        status=field_failure(status,&field,error,error_capacity);
        goto done;
    }
    praktor_yaml_field_error_dispose(&field);
    status=validate_variable_names(&builder,snapshot->metadata,"variables");
    if(status!=SALTS_OK)goto done;
    if(praktor_yaml_has(root,"statechart")) {
        status=praktor_yaml_get(root,"statechart",&snapshot->workflow_chart,NULL);
        if(status!=SALTS_OK)goto done;
    }
    if(compose) {
        builder.remaining_bytes=MAX_WORKFLOW_BYTES-yaml_source.len;
        status=load_libraries(&builder,root);
        if(status!=SALTS_OK)goto done;
    }
    for(size_t i=0u;i<count;++i) {
        status=admit_task(&builder,praktor_yaml_at(tasks,i),compose);
        if(status!=SALTS_OK)goto done;
    }
    count=snapshot->task_count;
    views=(const json_value_t **)malloc(count*sizeof(*views));
    if(!views) {
        status=snapshot_fail(error,error_capacity,SALTS_ENOMEM,
            "Cannot allocate native workflow graph views");
        goto done;
    }
    for(size_t i=0;i<count;++i)views[i]=snapshot->tasks[i];
    status=praktor_workflow_dependencies_create(views,count,&snapshot->graph,
        error,error_capacity);
    if(status!=SALTS_OK)goto done;
    if(snapshot->charts || praktor_yaml_valid(snapshot->workflow_chart))
        snapshot->chart_document=document;
    else praktor_yaml_document_destroy(document);
    free_libraries(&builder);
    free(views);
    *out=snapshot;
    return SALTS_OK;
done:
    praktor_yaml_field_error_dispose(&field);
    praktor_yaml_document_destroy(document);
    free_libraries(&builder);
    free(views);
    praktor_workflow_snapshot_destroy(snapshot);
    return status;
}
int praktor_workflow_snapshot_create_inline(vstr yaml_source,
    praktor_workflow_snapshot **out,char *error,size_t error_capacity) {
    return create_snapshot(yaml_source,PRAKTOR_WORKFLOW_MAX_TASKS,false,out,error,error_capacity);
}

int praktor_workflow_snapshot_create_for_run(vstr yaml_source,size_t limit,
    praktor_workflow_snapshot **out,char *error,size_t error_capacity) {
    return create_snapshot(yaml_source,limit,true,out,error,error_capacity);
}
size_t praktor_workflow_snapshot_task_count(const praktor_workflow_snapshot *snapshot) {
    return snapshot?snapshot->task_count:0u;
}
const json_value_t *praktor_workflow_snapshot_metadata(
    const praktor_workflow_snapshot *snapshot) {
    return snapshot?snapshot->metadata:NULL;
}
const json_value_t *praktor_workflow_snapshot_task_metadata(
    const praktor_workflow_snapshot *snapshot,size_t index) {
    return snapshot&&index<snapshot->task_count?snapshot->tasks[index]:NULL;
}
const praktor_orch_tree *praktor_workflow_snapshot_task_tree(
    const praktor_workflow_snapshot *snapshot,size_t index) {
    return snapshot&&index<snapshot->task_count?snapshot->trees[index]:NULL;
}
praktor_yaml_node praktor_workflow_snapshot_statechart(
    const praktor_workflow_snapshot *snapshot) {
    return snapshot?snapshot->workflow_chart:(praktor_yaml_node){0};
}
praktor_yaml_node praktor_workflow_snapshot_task_statechart(
    const praktor_workflow_snapshot *snapshot,size_t index) {
    if(snapshot&&snapshot->charts&&index<snapshot->task_count)return snapshot->charts[index];
    return (praktor_yaml_node){0};
}
int praktor_workflow_snapshot_task_kind_at(
    const praktor_workflow_snapshot *snapshot,size_t index,
    praktor_workflow_snapshot_task_kind *out) {
    if(!snapshot||!out)return SALTS_EINVAL;
    if(index>=snapshot->task_count)return SALTS_ENOENT;
    *out=snapshot->kinds[index];
    return SALTS_OK;
}
const praktor_workflow_dependencies *praktor_workflow_snapshot_dependencies(
    const praktor_workflow_snapshot *snapshot) {
    return snapshot?snapshot->graph:NULL;
}
