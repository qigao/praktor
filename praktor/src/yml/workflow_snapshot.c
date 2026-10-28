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

struct praktor_workflow_snapshot {
    json_value_t *metadata;
    json_value_t **tasks;
    praktor_orch_tree **trees;
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
    if(!runner_name)
        return snapshot_fail(error,capacity,SALTS_EINVAL,
            "Native workflow task must declare a supported runner");
    if(strcmp(runner_name,"command")==0) {
        *kind=PRAKTOR_WORKFLOW_SNAPSHOT_COMMAND;
        return command_tree(task,out,error,capacity);
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

int praktor_workflow_snapshot_create_inline(vstr yaml_source,
    praktor_workflow_snapshot **out,char *error,size_t error_capacity) {
    praktor_yaml_document *document=NULL;
    praktor_yaml_node root,tasks={0};
    praktor_workflow_snapshot *snapshot=NULL;
    praktor_yaml_field_error field={0};
    praktor_yaml_diagnostic yaml_error={error,error_capacity,PRAKTOR_YAML_FAILURE_NONE};
    const json_value_t **views=NULL;
    size_t i,count=0;
    int status;
    if(error&&error_capacity)error[0]='\0';
    if(!out||(!error&&error_capacity)||!vstr_is_valid(yaml_source)||
       yaml_source.len==0)
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
    if(praktor_yaml_has(root,"includes")) {
        status=snapshot_fail(error,error_capacity,SALTS_EINVAL,
            "inline workflows cannot declare includes");
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
    if(!count||count>PRAKTOR_WORKFLOW_MAX_TASKS) {
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
    snapshot->task_count=count;
    snapshot->tasks=(json_value_t **)calloc(count,sizeof(*snapshot->tasks));
    snapshot->trees=(praktor_orch_tree **)calloc(count,sizeof(*snapshot->trees));
    snapshot->kinds=(praktor_workflow_snapshot_task_kind *)calloc(count,
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
    for(i=0;i<count;++i) {
        const praktor_yaml_node task=praktor_yaml_at(tasks,i);
        status=praktor_yaml_task_metadata_create(task,&snapshot->tasks[i],&field);
        if(status!=SALTS_OK) {
            status=field_failure(status,&field,error,error_capacity);
            goto done;
        }
        praktor_yaml_field_error_dispose(&field);
        status=task_tree(task,snapshot->tasks[i],&snapshot->trees[i],
            &snapshot->kinds[i],error,error_capacity);
        if(status!=SALTS_OK)goto done;
    }
    views=(const json_value_t **)malloc(count*sizeof(*views));
    if(!views) {
        status=snapshot_fail(error,error_capacity,SALTS_ENOMEM,
            "Cannot allocate native workflow graph views");
        goto done;
    }
    for(i=0;i<count;++i)views[i]=snapshot->tasks[i];
    status=praktor_workflow_dependencies_create(views,count,&snapshot->graph,
        error,error_capacity);
    if(status!=SALTS_OK)goto done;
    praktor_yaml_document_destroy(document);
    free(views);
    *out=snapshot;
    return SALTS_OK;
done:
    praktor_yaml_field_error_dispose(&field);
    praktor_yaml_document_destroy(document);
    free(views);
    praktor_workflow_snapshot_destroy(snapshot);
    return status;
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
