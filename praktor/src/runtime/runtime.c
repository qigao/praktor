/* C11-only Praktor owner. Dynamic Plugin lease ownership is reserved for
 * ServiceHost; the current static provider is held by Configurator. */
#include "praktor/runtime.h"
#include "component_host.h"
#include "statechart_internal.h"
#include "yml/workflow_snapshot.h"
#include "yml/yaml_workflow_metadata.h"
#include "yml/workflow_dag_state.h"
#include "yml/workflow_task_registry.h"
#include "actions/orch_session_plan.h"
#include "actions/native_host.h"
#include "actions/tool.h"
#include "actions/variable_reference.h"
#include "actions/workflow_snapshot_values.h"
#include "actions/workflow_parallel_commit.h"
#include <cflow/executor.h>
#include <cmeta/ace_interceptor.h>
#include <cmeta/cleanup.h>
#include <praktor/process_retainer.h>
#include <core/clock.h>
#include <core/error_codes.h>
#include <json_parser.h>
#include <core/vstr.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRK_MAX_TASKS 64u
#define PRK_MAX_VALUE 4096u
#define PRK_MAX_KEY 128u
enum { PRK_EXIT_CODE, PRK_STDOUT, PRK_STDERR, PRK_PROCESS_OUTPUTS };
typedef struct prk_task_outputs {
    const char *keys[PRK_PROCESS_OUTPUTS];
    const json_value_t *declared;
    bool process;
} prk_task_outputs;

typedef struct prk_state {
    praktor_workflow_snapshot *snapshot;
    praktor_workflow_task_registry *registry;
    praktor_workflow_dag_state *dag;
    praktor_value_store root;
    praktor_process_retainer retainer;
    bool has_retainer;
} prk_state;
static void clean_snapshot(void *u,void *slot) {
    (void)u;
    praktor_workflow_snapshot **p=(praktor_workflow_snapshot **)slot;
    praktor_workflow_snapshot_destroy(*p);*p=NULL;
}
static void clean_registry(void *u,void *slot) {
    (void)u;
    praktor_workflow_task_registry **p=(praktor_workflow_task_registry **)slot;
    praktor_workflow_task_registry_destroy(*p);*p=NULL;
}
static void clean_dag(void *u,void *slot) {
    (void)u;
    praktor_workflow_dag_state **p=(praktor_workflow_dag_state **)slot;
    praktor_workflow_dag_state_destroy(*p);*p=NULL;
}
static void clean_root(void *u,void *slot) {
    (void)u;
    praktor_value_store_destroy((praktor_value_store *)slot);
}
void praktor_runtime_result_destroy(praktor_runtime_result *result) {
    if(!result)return;
    if(result->json)json_serialize_free(result->json);
    *result=(praktor_runtime_result){0};
}
static int drain(praktor_process_retainer *retainer,uint64_t budget_ns) {
    uint64_t started=hrtime();
    size_t pending=SIZE_MAX;
    do {
        size_t reaped=0u;
        int status=praktor_process_retainer_run_ready(
            retainer,64u,&reaped,&pending);
        if(status!=SALTS_OK&&status!=SALTS_EBUSY)return status;
        if(status==SALTS_OK&&pending==0u)return SALTS_OK;
        sleep_ms(1u);
    }while(hrtime()-started<budget_ns);
    return SALTS_EBUSY;
}
typedef struct prk_task_driver {
    praktor_session *session;
    praktor_statechart *chart,*workflow_chart;
    const char *task_name,*chart_error;
    salts_bt_node_status outcome;
    bool ran;
} prk_task_driver;
typedef struct prk_chart_views {
    const praktor_statechart *task,*workflow;
} prk_chart_views;
static int chart_active(void *user,const char *key) {
    const prk_chart_views *views=user;
    if(views->task&&!strncmp(key,"task.",5u))
        return praktor_statechart_active(views->task,key+5u);
    if(views->workflow&&!strncmp(key,"workflow.",9u))
        return praktor_statechart_active(views->workflow,key+9u);
    return -1;
}
typedef struct prk_chart_actions {
    praktor_bt_host base;
    praktor_statechart *task,*workflow;
} prk_chart_actions;
static int task_leaf_tick(void *user,const praktor_leaf_request *request,
                         salts_bt_node_status *status) {
    const prk_chart_actions *actions=user;
    if(strcmp(request->action,"Raise"))
        return actions->base.tick_typed_leaf(actions->base.user,request,status);
    if(request->in_parallel_branch)return SALTS_ENOTSUP;
    const char *event=NULL,*bindings=NULL;
    praktor_statechart *chart=actions->task;
    for(size_t i=0u;i<request->parameter_count;++i) {
        const praktor_leaf_parameter *parameter=&request->parameters[i];
        if(!strcmp(parameter->name,"event"))event=parameter->text;
        else if(!strcmp(parameter->name,"with"))bindings=parameter->text;
        else if(!strcmp(parameter->name,"scope")&&!strcmp(parameter->text,"workflow"))
            chart=actions->workflow;
    }
    int rc=praktor_statechart_raise(chart,&actions->base,event,bindings);
    if(rc==SALTS_OK)*status=SALTS_BT_SUCCESS;
    else if(request->error&&request->error_capacity)
        (void)snprintf(request->error,request->error_capacity,"%s",
                       praktor_statechart_error(chart));
    return rc;
}
static void task_leaf_halt(void *user,const praktor_leaf_request *request) {
    const prk_chart_actions *actions=user;
    actions->base.halt_typed_leaf(actions->base.user,request);
}
static int observe_task(prk_task_driver *driver,const char *event,const char *status) {
    praktor_statechart *const charts[]={driver->workflow_chart,driver->chart};
    for(size_t i=0u;i<sizeof charts/sizeof charts[0];++i) {
        if(*praktor_statechart_error(charts[i]))continue;
        int rc=praktor_statechart_lifecycle(charts[i],event,driver->task_name,status);
        if(rc!=SALTS_OK) {
            driver->chart_error=praktor_statechart_error(charts[i]);
            return rc;
        }
    }
    return SALTS_OK;
}
typedef struct prk_running_task {
    praktor_native_host host;
    praktor_session session;
    praktor_statechart chart;
    prk_chart_views views;
    prk_chart_actions chart_actions;
    praktor_tool_action tool;
    prk_task_driver driver;
} prk_running_task;

typedef struct prk_cohort_driver {
    prk_running_task *tasks;
    size_t count,failed_slot;
    praktor_cancel_probe cancel;
    void *cancel_context;
    praktor_event_poll event_poll;
    void *event_context;
    uint64_t deadline;
    char *error;
    size_t error_capacity;
    praktor_runtime_status status;
    bool ran,finalized;
} prk_cohort_driver;

static int tick_task(prk_task_driver *task,const prk_cohort_driver *cohort) {
    int rc;
    if(!task->ran) {
        task->ran=true;
        rc=observe_task(task,"task.started","running");
        if(rc!=SALTS_OK)return rc;
    }
    praktor_statechart *const charts[]={task->workflow_chart,task->chart};
    for(size_t i=0u;i<sizeof charts/sizeof charts[0];++i) {
        rc=praktor_statechart_advance(charts[i]);
        if(rc!=SALTS_OK) {
            task->chart_error=praktor_statechart_error(charts[i]);
            return rc;
        }
        if(!cohort->event_poll||!praktor_statechart_polls_external(charts[i]))continue;
        const char *target=i?task->task_name:NULL;
        praktor_host_event notification={0};
        const int available=cohort->event_poll(cohort->event_context,target,&notification);
        if(available==0)continue;
        if(available!=1) {
            (void)snprintf(cohort->error,cohort->error_capacity,
                "event source for '%s' returned %d",target?target:"workflow",available);
            return SALTS_EIO;
        }
        rc=praktor_statechart_receive(charts[i],&notification);
        if(rc!=SALTS_OK) {
            task->chart_error=praktor_statechart_error(charts[i]);
            return rc;
        }
    }
    return praktor_session_tick(task->session,&task->outcome);
}

/* One CFlow descriptor owns the entire cooperative turn. No worker accesses
 * the DAG, registry, or mutable workflow values during this callback. */
static void callback_run(void *p) {
    prk_cohort_driver *c=p;
    c->ran=true;
    size_t remaining=c->count;
    while(remaining&&c->status==PRAKTOR_RUNTIME_OK) {
        for(size_t i=0u;i<c->count;++i) {
            prk_task_driver *task=&c->tasks[i].driver;
            if(task->outcome==SALTS_BT_SUCCESS)continue;
            if(c->cancel&&c->cancel(c->cancel_context)) {
                c->status=PRAKTOR_RUNTIME_CANCELLED;
                break;
            }
            if(hrtime()>=c->deadline) {
                c->status=PRAKTOR_RUNTIME_DEADLINE;
                break;
            }
            const int rc=tick_task(task,c);
            if(rc!=SALTS_OK||task->outcome==SALTS_BT_FAILURE) {
                c->status=PRAKTOR_RUNTIME_FAILED;
                c->failed_slot=i;
                if(!c->error[0])
                    (void)snprintf(c->error,c->error_capacity,"%s",
                        task->chart_error?task->chart_error:praktor_session_error(task->session));
                break;
            }
            if(task->outcome==SALTS_BT_SUCCESS)--remaining;
        }
        if(remaining&&c->status==PRAKTOR_RUNTIME_OK)sleep_ms(1u);
    }
    /* Closing happens on the owner after CFlow has finalized the callback. */
}
static void callback_cancel(void *p) {
    ((prk_cohort_driver *)p)->status=PRAKTOR_RUNTIME_CANCELLED;
}
static void callback_finalize(void *p) {
    ((prk_cohort_driver *)p)->finalized=true;
}

typedef struct prk_admission {
    praktor_workflow_dag_state *dag;
    const size_t *ids;
    size_t count;
    cflow_executor *executor;
    cflow_executor_task descriptor;
} prk_admission;
typedef struct prk_ack { bool accepted; } prk_ack;
CMETA_INTERCEPTOR_TYPE(prk_action_gate,prk_admission,prk_ack);
static cmeta_status before_admit(void *ctx,const prk_admission *request,
                                  bool *proceed) {
    const praktor_component_host *component=(const praktor_component_host *)ctx;
    if(!component||!component->active||component->admitted!=1)
        return CMETA_CALLBACK_ERROR;
    *proceed=false;
    for(size_t i=0u;i<request->count;++i) {
        bool reserved=false;
        if(praktor_workflow_dag_is_reserved(request->dag,request->ids[i],&reserved)!=SALTS_OK)
            return CMETA_CALLBACK_ERROR;
        if(!reserved)return CMETA_OK;
    }
    *proceed=true;
    return CMETA_OK;
}
static cmeta_status admit_action(void *ctx,const prk_admission *request,
                                  prk_ack *response) {
    (void)ctx;
    response->accepted=cflow_executor_try_post_task(
        request->executor,&request->descriptor)==CFLOW_ADMISSION_ACCEPTED;
    return response->accepted?CMETA_OK:CMETA_CALLBACK_ERROR;
}
CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(&admit_action,prk_action_gate_target_fn),
    "CMeta Interceptor must preserve exact C11 admission ABI");

/* Explicit native action selection, not a fallback sequence. Preflight every
 * plan before executing any DAG node. */
static int make_plan(const praktor_workflow_snapshot *snapshot,size_t id,bool has_chart,
    praktor_orch_session_plan **out,char *error,size_t capacity) {
    praktor_workflow_snapshot_task_kind kind;
    praktor_session_config cfg={0};
    const praktor_orch_tree *tree=praktor_workflow_snapshot_task_tree(snapshot,id);
    const praktor_orch_node *node=praktor_orch_tree_root(tree);
    if(!node||praktor_workflow_snapshot_task_kind_at(snapshot,id,&kind)!=SALTS_OK)
        return SALTS_EINVAL;
    praktor_session_config_init(&cfg);
    if(kind==PRAKTOR_WORKFLOW_SNAPSHOT_COMMAND)
        return praktor_orch_session_plan_create_native_command(
            tree,&cfg.tree,out,error,capacity);
    switch(praktor_orch_node_type(node)) {
    case PRAKTOR_ORCH_SHELL:
        return praktor_orch_session_plan_create_native_shell(
            tree,&cfg.tree,out,error,capacity);
    case PRAKTOR_ORCH_SET_VARIABLE:
    case PRAKTOR_ORCH_TOOL:
        return praktor_orch_session_plan_create(
            tree,&cfg.tree,out,error,capacity);
    default:
        if(has_chart)return praktor_orch_session_plan_create_chart_task(
            tree,&cfg.tree,out,error,capacity);
        return praktor_orch_session_plan_create_workflow_tree(
            tree,&cfg.tree,out,error,capacity);
    }
}
typedef struct prk_task_tools {
    praktor_tool_binding *bindings;
    size_t count;
} prk_task_tools;

static int bind_tools(praktor_component_host *components,const praktor_orch_session_plan *plan,
    prk_task_tools *tools,char *error,size_t capacity) {
    const size_t nodes=praktor_orch_session_plan_node_count(plan);
    size_t count=0u,used=0u;
    for(size_t i=0u;i<nodes;++i)
        if(!strcmp(praktor_orch_session_plan_node_at(plan,i)->action,"Tool"))++count;
    if(!count)return SALTS_OK;
    tools->bindings=calloc(count,sizeof(*tools->bindings));
    if(!tools->bindings)return SALTS_ENOMEM;
    tools->count=count;
    for(size_t i=0u;i<nodes;++i) {
        const praktor_session_node *node=praktor_orch_session_plan_node_at(plan,i);
        if(strcmp(node->action,"Tool"))continue;
        const char *service=NULL,*payload=NULL;
        size_t payload_size=0u;
        for(size_t p=0u;p<node->parameter_count;++p) {
            if(!strcmp(node->parameters[p].name,"service"))service=node->parameters[p].text;
            if(!strcmp(node->parameters[p].name,"with")) {
                payload=node->parameters[p].text;
                payload_size=node->text_sizes[p];
            }
        }
        praktor_tool_binding *binding=&tools->bindings[used++];
        binding->node_index=i;
        if(!service||praktor_component_host_service(components,service,&binding->service,
                                                      &binding->definition)!=SALTS_OK) {
            (void)snprintf(error,capacity,"unknown tool service '%s'",service?service:"");
            return SALTS_EINVAL;
        }
        if(payload) {
            binding->inputs=json_parse(payload,payload_size);
            if(!binding->inputs) {
                (void)snprintf(error,capacity,"cannot decode Tool inputs");
                return SALTS_EINVAL;
            }
        }
        int rc=praktor_tool_admit(binding->definition,binding->inputs,error,capacity);
        if(rc!=SALTS_OK)return rc;
    }
    return SALTS_OK;
}
static vstr action_parameter(const praktor_orch_node *node,const char *name) {
    for(size_t i=0u;i<praktor_orch_param_count(node);++i) {
        praktor_orch_param param={0};
        if(praktor_orch_param_at(node,i,&param)!=SALTS_OK)break;
        if(vstr_eq(param.key,vstr_from_cstr(name)))return param.value;
    }
    return (vstr){0};
}
static int bind_outputs(const praktor_orch_node *root,bool command,
    const json_value_t *task,size_t value_capacity,prk_task_outputs *out,
    char *error,size_t capacity) {
    const praktor_orch_type type=praktor_orch_node_type(root);
    const char *name=json_get_string(task,"name");
    out->declared=json_object_get(task,"outputs");
    const size_t count=json_array_size(out->declared);
    if(count>PRAKTOR_WORKFLOW_MAX_TEXT_OUTPUTS||count>value_capacity) {
        (void)snprintf(error,capacity,"task '%s': too many declared outputs",name);
        return SALTS_EINVAL;
    }
    for(size_t i=0u;i<count;++i) {
        const json_value_t *item=json_array_get(out->declared,i);
        const vstr ref={json_string(item),json_string_len(item)};
        const vstr key=praktor_variable_reference(ref);
        if(!key.data||key.len>PRK_MAX_KEY) {
            (void)snprintf(error,capacity,"task '%s': outputs require $name references",name);
            return SALTS_EINVAL;
        }
        for(size_t j=0u;j<i;++j) {
            if(strcmp(ref.data,json_string(json_array_get(out->declared,j)))==0) {
                (void)snprintf(error,capacity,"task '%s': duplicate output '%s'",name,ref.data);
                return SALTS_EINVAL;
            }
        }
    }
    out->process=command||type==PRAKTOR_ORCH_SHELL||type==PRAKTOR_ORCH_TOOL;
    if(out->process) {
        out->keys[PRK_EXIT_CODE]="shell_exit_code";
        out->keys[PRK_STDOUT]=command?"stdout":"shell_output";
        out->keys[PRK_STDERR]="shell_stderr";
    }
    static const char *const params[]={"exit_code_key","output_key","stderr_key"};
    const size_t bindings=type==PRAKTOR_ORCH_SHELL?PRK_PROCESS_OUTPUTS:
                          type==PRAKTOR_ORCH_SET_VARIABLE?1u:0u;
    for(size_t i=0u;i<bindings;++i) {
        const char *parameter=type==PRAKTOR_ORCH_SET_VARIABLE?"key":params[i];
        const vstr key=action_parameter(root,parameter);
        if(!key.data)continue;
        if(!key.len||key.len>PRK_MAX_KEY||key.data[0]=='$'||
           memchr(key.data,'\0',key.len)!=NULL) {
            (void)snprintf(error,capacity,"task '%s': '%s' must name a literal output key",name,parameter);
            return SALTS_EINVAL;
        }
        out->keys[i]=key.data;
    }
    if(out->process) {
        for(size_t i=0u;i<PRK_PROCESS_OUTPUTS;++i) {
            if(strcmp(out->keys[i],"shell_error")==0) {
                (void)snprintf(error,capacity,"task '%s': 'shell_error' is reserved for diagnostics",name);
                return SALTS_EINVAL;
            }
            for(size_t j=0u;j<i;++j)
                if(strcmp(out->keys[i],out->keys[j])==0) {
                    (void)snprintf(error,capacity,"task '%s': Shell output keys must be distinct",name);
                    return SALTS_EINVAL;
                }
        }
    }
    return SALTS_OK;
}
static bool add_owned(json_value_t *parent,const char *key,
                      json_value_t *child) {
    if(!child)return false;
    if(json_object_add_n(parent,key,strlen(key),child))return true;
    json_free(child);return false;
}
static bool add_text(json_value_t *parent,const char *key,const char *value) {
    return add_owned(parent,key,json_create_string_n(value,strlen(value)));
}
static int read_output(praktor_value_store *store,const char *key,
                       char *data,size_t capacity,size_t *length) {
    if(praktor_value_store_get(store,key,data,capacity,length)!=
            PRAKTOR_VALUE_OK)return SALTS_ENOENT;
    if(*length>=capacity||memchr(data,'\0',*length))return SALTS_EINVAL;
    data[*length]='\0';
    return SALTS_OK;
}
static size_t output_count(const prk_task_outputs *bindings) {
    return bindings->declared?json_array_size(bindings->declared):
        bindings->process?PRK_PROCESS_OUTPUTS:bindings->keys[0]?1u:0u;
}
static const char *output_key(const prk_task_outputs *bindings,size_t index) {
    return bindings->declared?json_string(json_array_get(bindings->declared,index))+1u:
        bindings->keys[index];
}

static int stage_outputs(praktor_value_store *root,praktor_value_store *scope,
    const prk_task_outputs *bindings,praktor_value_txn *txn,json_value_t **out,
    char *error,size_t capacity) {
    static const char *const process_labels[]={"exit_code","stdout","stderr"};
    const json_value_t *declared=bindings->declared;
    const size_t count=output_count(bindings);
    if(!count)return SALTS_OK;
    const char *ports[PRAKTOR_WORKFLOW_MAX_TEXT_OUTPUTS];
    char value[PRK_MAX_VALUE+1u];
    json_value_t *typed=json_create_object();
    int rc=SALTS_EINVAL;
    if(!typed)return SALTS_ENOMEM;
    for(size_t i=0u;i<count;++i) {
        ports[i]=output_key(bindings,i);
        const char *label=declared||!bindings->process?ports[i]:process_labels[i];
        size_t length=0u;
        rc=read_output(scope,ports[i],value,sizeof(value),&length);
        if(rc!=SALTS_OK) {
            (void)snprintf(error,capacity,"output '%s' is unavailable",ports[i]);
            goto done;
        }
        if(!add_owned(typed,label,json_create_string_n(value,length))) {
            rc=SALTS_ENOMEM;
            goto done;
        }
    }
    rc=praktor_workflow_prepare_task_text_outputs(root,scope,ports,count,txn);
    if(rc==PRAKTOR_VALUE_OK) {
        *out=typed;
        return SALTS_OK;
    }
    if(rc==PRAKTOR_VALUE_ENOMEM)rc=SALTS_ENOMEM;
    else if(rc==PRAKTOR_VALUE_ENOTFOUND)rc=SALTS_ENOENT;
    else rc=SALTS_ENOBUFS;
done:
    json_free(typed);
    return rc;
}
static int publish_failure_exit(prk_state *s,size_t index,
                                praktor_value_store *scope,const char *key) {
    char code[64]={0};size_t n=0u;
    if(read_output(scope,key,code,sizeof(code),&n)!=SALTS_OK)
        return SALTS_ENOENT;
    json_value_t *value=json_create_string_n(code,n);
    if(!value)return SALTS_ENOMEM;
    int rc=praktor_workflow_task_registry_set_output(
        s->registry,index,vstr_from_cstr("exit_code"),value);
    json_free(value);
    return rc;
}
static int marshal_result(prk_state *s,praktor_runtime_result *out,
                          praktor_runtime_status status) {
    json_value_t *root=json_create_object();
    json_value_t *tasks=json_create_object();
    if(!root||!tasks)goto failed;
    const char *terminal=status==PRAKTOR_RUNTIME_OK?"success":
        status==PRAKTOR_RUNTIME_CANCELLED?"cancelled":
        status==PRAKTOR_RUNTIME_DEADLINE?"timed_out":"failed";
    if(!add_text(root,"workflow_status",terminal))goto failed;
    size_t count=praktor_workflow_task_registry_count(s->registry);
    for(size_t i=0u;i<count;++i) {
        praktor_workflow_task_state state;
        vstr name={0};
        if(praktor_workflow_task_registry_state(s->registry,i,&state)!=SALTS_OK ||
           praktor_workflow_task_registry_name(s->registry,i,&name)!=SALTS_OK)
            goto failed;
        const char *state_name=state==PRAKTOR_WORKFLOW_TASK_PENDING?"pending":
            state==PRAKTOR_WORKFLOW_TASK_RUNNING?"running":
            state==PRAKTOR_WORKFLOW_TASK_SUCCESS?"success":
            state==PRAKTOR_WORKFLOW_TASK_FAILED?"failed":"skipped";
        json_value_t *task=json_create_object();
        if(!task)goto failed;
        if(!add_text(task,"status",state_name)){
            json_free(task);goto failed;
        }
        const json_value_t *values=NULL;
        if(praktor_workflow_task_registry_get_outputs(
                s->registry,i,&values)==SALTS_OK&&values) {
            if(!add_owned(task,"outputs",json_clone(values))){
                json_free(task);goto failed;
            }
        }
        if(!json_object_add_n(tasks,name.data,name.len,task)){
            json_free(task);goto failed;
        }
    }
    if(!add_owned(root,"tasks",tasks)){tasks=NULL;goto failed;}
    tasks=NULL;
    out->json=json_serialize(root,&out->json_size);
    json_free(root);
    return out->json?SALTS_OK:SALTS_ENOMEM;
failed:
    json_free(tasks);json_free(root);
    return SALTS_ENOMEM;
}
static int validate_selected_task_fields(const praktor_workflow_snapshot *snapshot,
        const praktor_workflow_dag_state *dag,char *error,size_t capacity) {
    static const struct {const char *key,*yaml;} unsupported[]={
        {"when","when"},{"triggers","triggers"},{"env","env"},{"dot_env","dotEnv"},
        {"each","each"},{"timeout","timeout"},{"retry_count","retries"},
        {"working_dir","working_dir"},{"silent","silent"},{"sources","sources"},
        {"generates","generates"},{"finally","finally"}
    };
    const size_t count=praktor_workflow_snapshot_task_count(snapshot);
    for(size_t i=0u;i<count;++i) {
        bool selected=false;
        const int status=praktor_workflow_dag_is_selected(dag,i,&selected);
        if(status!=SALTS_OK)return status;
        if(!selected)continue;
        const json_value_t *task=praktor_workflow_snapshot_task_metadata(snapshot,i);
        for(size_t j=0u;j<sizeof unsupported/sizeof unsupported[0];++j) {
            if(json_object_get(task,unsupported[j].key)) {
                (void)snprintf(error,capacity,"task '%s': field '%s' is unsupported",
                               json_get_string(task,"name"),unsupported[j].yaml);
                return SALTS_ENOTSUP;
            }
        }
    }
    return SALTS_OK;
}

typedef struct prk_cohort_context {
    prk_state *owner;
    praktor_component_host *components;
    praktor_statechart *workflow_chart;
    const praktor_session_config *defaults;
    praktor_orch_session_plan *const *plans;
    praktor_statechart_plan *const *charts;
    const prk_task_tools *tools;
    const prk_task_outputs *outputs;
    size_t value_capacity;
    praktor_cancel_probe cancel;
    void *cancel_context;
    praktor_event_poll event_poll;
    void *event_context;
    uint64_t deadline;
} prk_cohort_context;

static bool check_output_keys(const prk_task_outputs *outputs,const size_t *ids,
    size_t count,char *error,size_t capacity) {
    for(size_t i=0u;i<count;++i) {
        const prk_task_outputs *a=&outputs[ids[i]];
        for(size_t j=i+1u;j<count;++j) {
            const prk_task_outputs *b=&outputs[ids[j]];
            for(size_t p=0u;p<output_count(a);++p) {
                const char *key=output_key(a,p);
                for(size_t q=0u;q<output_count(b);++q) {
                    if(strcmp(key,output_key(b,q))==0) {
                        (void)snprintf(error,capacity,"ready tasks share output key '%s'",key);
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

static int prepare_task(const prk_cohort_context *ctx,size_t index,prk_running_task *task) {
    prk_state *owner=ctx->owner;
    int rc=praktor_workflow_task_registry_start(owner->registry,index);
    if(rc!=SALTS_OK)return rc;
    praktor_native_host_config hostconfig;
    praktor_native_host_config_init(&hostconfig,ctx->value_capacity,PRK_MAX_KEY,PRK_MAX_VALUE);
    hostconfig.max_running_leaves=1u;
    hostconfig.reserve_event_fields=ctx->charts[index]!=NULL;
    task->views=(prk_chart_views){.task=&task->chart,.workflow=ctx->workflow_chart};
    if(ctx->charts[index]||ctx->workflow_chart->impl) {
        hostconfig.chart_active=chart_active;
        hostconfig.chart_user=&task->views;
    }
    rc=praktor_native_host_init_with_config(&task->host,&hostconfig);
    if(rc!=SALTS_OK)return rc;
    rc=praktor_workflow_load_task_text_scope(owner->snapshot,index,&owner->root,
                                            praktor_native_host_values(&task->host));
    if(rc!=PRAKTOR_VALUE_OK)return rc;
    praktor_session_config config=*ctx->defaults;
    rc=praktor_native_host_bind(&task->host,&config.host);
    if(rc!=SALTS_OK)return rc;
    if(ctx->tools[index].count)
        praktor_tool_action_bind(&task->tool,ctx->tools[index].bindings,
                                 ctx->tools[index].count,&config.host);
    config.retainer=&owner->retainer;
    config.has_capability=praktor_component_host_has_capability;
    config.capability_user=ctx->components;
    if(ctx->charts[index]) {
        rc=praktor_statechart_init(&task->chart,ctx->charts[index],&task->host,&config);
        if(rc!=SALTS_OK)return rc;
    }
    if(ctx->charts[index]||ctx->workflow_chart->impl) {
        task->chart_actions=(prk_chart_actions){
            .base=config.host,.task=&task->chart,.workflow=ctx->workflow_chart
        };
        config.host.user=&task->chart_actions;
        config.host.tick_typed_leaf=task_leaf_tick;
        config.host.halt_typed_leaf=task_leaf_halt;
    }
    task->driver=(prk_task_driver){
        .session=&task->session,
        .chart=&task->chart,
        .workflow_chart=ctx->workflow_chart,
        .task_name=json_get_string(praktor_workflow_snapshot_task_metadata(owner->snapshot,index),"name"),
        .outcome=SALTS_BT_RUNNING
    };
    return praktor_orch_session_plan_init_session(ctx->plans[index],&config,&task->session);
}

static int publish_cohort(const prk_cohort_context *ctx,const size_t *ids,
    prk_running_task *tasks,size_t count,char *error,size_t capacity) {
    praktor_workflow_task_output_update updates[PRAKTOR_WORKFLOW_MAX_PARALLELISM]={0};
    praktor_value_txn staged[PRAKTOR_WORKFLOW_MAX_PARALLELISM]={0};
    json_value_t *values[PRAKTOR_WORKFLOW_MAX_PARALLELISM]={0};
    size_t used=0u;
    int rc=SALTS_OK;
    for(size_t i=0u;i<count;++i) {
        rc=stage_outputs(&ctx->owner->root,
            praktor_native_host_values(&tasks[i].host),
            &ctx->outputs[ids[i]],&staged[used],&values[used],error,capacity);
        if(rc!=SALTS_OK)break;
        if(values[used]) {
            updates[used]=(praktor_workflow_task_output_update){
                .task_index=ids[i],.updates=values[used]
            };
            ++used;
        }
    }
    if(rc==SALTS_OK&&used) {
        const int committed=praktor_workflow_parallel_commit(ctx->owner->registry,updates,staged,used);
        if(committed!=PRAKTOR_PARALLEL_OK) {
            (void)snprintf(error,capacity,"could not commit task outputs (status %d)",committed);
            rc=committed==PRAKTOR_PARALLEL_CONFLICT?SALTS_EINVAL:SALTS_ENOBUFS;
        }
    }
    for(size_t i=0u;i<used;++i) {
        praktor_value_txn_abort(&staged[i]);
        json_free(values[i]);
    }
    return rc;
}

static praktor_runtime_status run_cohort(const prk_cohort_context *ctx,
    const size_t *ids,size_t count,char *error,size_t capacity) {
    prk_state *owner=ctx->owner;
    prk_running_task tasks[PRAKTOR_WORKFLOW_MAX_PARALLELISM]={0};
    prk_cohort_driver driver={
        .tasks=tasks,.count=count,.failed_slot=SIZE_MAX,.cancel=ctx->cancel,
        .cancel_context=ctx->cancel_context,.deadline=ctx->deadline,
        .event_poll=ctx->event_poll,.event_context=ctx->event_context,
        .error=error,.error_capacity=capacity,.status=PRAKTOR_RUNTIME_OK
    };
    praktor_runtime_status status=PRAKTOR_RUNTIME_RESOURCE;
    cflow_executor executor={0};
    cflow_executor_control control={0};
    bool initialized=false,accepted=false;
    int rc=SALTS_OK;
    if(!check_output_keys(ctx->outputs,ids,count,error,capacity)) {
        status=PRAKTOR_RUNTIME_FAILED;
        goto settle;
    }
    for(size_t i=0u;i<count;++i) {
        rc=prepare_task(ctx,ids[i],&tasks[i]);
        if(rc!=SALTS_OK)goto settle;
    }
    if(!(initialized=cflow_executor_manual_init_with_capacity(&executor,1u))||
       !cflow_executor_as_control(&executor,&control)) {
        rc=SALTS_ENOMEM;goto settle;
    }
    const cflow_executor_task descriptor={
        callback_run,callback_cancel,callback_finalize,&driver
    };
    prk_admission admission={owner->dag,ids,count,&executor,descriptor};
    prk_ack ack={false};
    prk_action_gate_hook gate={ctx->components,before_admit,NULL,NULL};
    prk_action_gate chain={NULL,admit_action,&gate,1u};
    accepted=prk_action_gate_invoke(&chain,&admission,&ack)==CMETA_OK&&ack.accepted;
    if(!accepted) {
        rc=SALTS_EBUSY;goto settle;
    }
    const size_t invoked=cflow_executor_run_ready(&executor);
    if(!cflow_executor_control_shutdown(&control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN)||
       cflow_executor_control_wait_idle(&control)!=CFLOW_EXECUTOR_WAIT_IDLE)
        abort(); /* A live callback would still borrow this cohort. */
    if(invoked!=1u||!driver.ran||!driver.finalized) {
        rc=SALTS_EBUSY;goto settle;
    }
    status=driver.status;
settle:
    if(initialized)cflow_executor_destroy(&executor);
    for(size_t i=0u;i<count;++i) {
        if(praktor_session_destroy(&tasks[i].session)!=SALTS_OK||
           praktor_tool_action_close(&tasks[i].tool)!=SALTS_OK)
            abort(); /* A live native owner still borrows task storage. */
        if(rc==SALTS_OK&&tasks[i].tool.halt_status!=SALTS_OK)rc=tasks[i].tool.halt_status;
    }
    if(drain(&owner->retainer,UINT64_C(5000000000))!=SALTS_OK)abort();
    if(rc!=SALTS_OK) {
        status=PRAKTOR_RUNTIME_RESOURCE;
        if(!error[0])(void)snprintf(error,capacity,"task preparation or cleanup failed (status %d)",rc);
    }
    if(status==PRAKTOR_RUNTIME_OK&&hrtime()>=ctx->deadline)
        status=PRAKTOR_RUNTIME_DEADLINE;
    if(status==PRAKTOR_RUNTIME_OK) {
        rc=publish_cohort(ctx,ids,tasks,count,error,capacity);
        if(rc!=SALTS_OK)
            status=rc==SALTS_ENOENT||rc==SALTS_EINVAL?PRAKTOR_RUNTIME_FAILED:PRAKTOR_RUNTIME_RESOURCE;
    }else if(driver.failed_slot<count) {
        const size_t slot=driver.failed_slot,index=ids[slot];
        if(ctx->outputs[index].process)
            (void)publish_failure_exit(owner,index,praktor_native_host_values(&tasks[slot].host),
                                       ctx->outputs[index].keys[PRK_EXIT_CODE]);
    }
    const bool succeeded=status==PRAKTOR_RUNTIME_OK;
    if(!succeeded)
        (void)praktor_workflow_dag_stop(owner->dag,status==PRAKTOR_RUNTIME_CANCELLED?
            PRAKTOR_WORKFLOW_DAG_CANCELLED:status==PRAKTOR_RUNTIME_DEADLINE?
            PRAKTOR_WORKFLOW_DAG_DEADLINE:PRAKTOR_WORKFLOW_DAG_FAILED);
    for(size_t i=0u;i<count;++i) {
        rc=succeeded?praktor_workflow_task_registry_complete(owner->registry,ids[i]):
            praktor_workflow_task_registry_fail(owner->registry,ids[i],
                vstr_from_cstr("C11 task group failed, was cancelled or timed out"));
        const int settled=accepted?praktor_workflow_dag_finish(owner->dag,ids[i],succeeded):
            praktor_workflow_dag_refuse(owner->dag,ids[i]);
        if(rc!=SALTS_OK||settled!=SALTS_OK) {
            status=PRAKTOR_RUNTIME_RESOURCE;
            if(!error[0])(void)snprintf(error,capacity,"could not settle task %zu",ids[i]);
        }
    }
    const char *outcome=succeeded?"success":status==PRAKTOR_RUNTIME_CANCELLED?
        "cancelled":status==PRAKTOR_RUNTIME_DEADLINE?"deadline":"failed";
    for(size_t i=0u;i<count;++i) {
        if(tasks[i].driver.ran&&observe_task(&tasks[i].driver,"task.finished",outcome)!=SALTS_OK) {
            if(status==PRAKTOR_RUNTIME_OK)status=PRAKTOR_RUNTIME_FAILED;
            if(!error[0]) (void)snprintf(error,capacity,"%s",tasks[i].driver.chart_error);
        }
        if(praktor_statechart_destroy(&tasks[i].chart)!=SALTS_OK)abort();
        praktor_native_host_destroy(&tasks[i].host);
    }
    return status;
}

static praktor_runtime_status execute_yaml(
    const char *yaml,size_t yaml_size,const praktor_runtime_limits *specified,
    praktor_cancel_probe cancel,void *cancel_context,
     const char *selected_task,praktor_event_poll event_poll,void *event_context,
     praktor_runtime_result *out) {
    praktor_runtime_limits options=
        specified?*specified:praktor_runtime_limits_default();
    praktor_component_host components={0};
    prk_state owner={0};
    cmeta_cleanup cleanup[4]={{0}};
    praktor_orch_session_plan *plans[PRK_MAX_TASKS]={0};
    prk_task_outputs outputs[PRK_MAX_TASKS]={0};
    praktor_statechart_plan *charts[PRK_MAX_TASKS]={0};
    praktor_statechart_plan *workflow_plan=NULL;
    praktor_statechart workflow_chart={0};
    praktor_native_host workflow_host={0};
    prk_chart_views workflow_views={.workflow=&workflow_chart};
    prk_task_tools tools[PRK_MAX_TASKS]={0};
    praktor_runtime_status status=PRAKTOR_RUNTIME_INVALID;
    size_t task_count=0u;
    praktor_session_config defaults={0};
    uint64_t deadline=0u;
    int rc=SALTS_OK;
    if(!out||out->json||!yaml||!yaml_size||yaml_size>1024u*1024u ||
       options.max_tasks<1||options.max_tasks>(int)PRK_MAX_TASKS ||
       options.value_capacity<1||options.value_capacity>4096||
       options.timeout_ms<1)return PRAKTOR_RUNTIME_INVALID;
    out->status=PRAKTOR_RUNTIME_INVALID;
    out->error[0]='\0';
    deadline=hrtime()+(uint64_t)options.timeout_ms*UINT64_C(1000000);
    (void)cmeta_cleanup_arm(&cleanup[0],clean_snapshot,NULL,&owner.snapshot);
    (void)cmeta_cleanup_arm(&cleanup[1],clean_registry,NULL,&owner.registry);
    (void)cmeta_cleanup_arm(&cleanup[2],clean_dag,NULL,&owner.dag);
    rc=praktor_workflow_snapshot_create_for_run(vstr_from_buf(yaml,yaml_size),
         (size_t)options.max_tasks,
         &owner.snapshot,out->error,sizeof(out->error));
    if(rc!=SALTS_OK) {
        if(rc==SALTS_ENOTSUP)status=PRAKTOR_RUNTIME_UNSUPPORTED;
        else if(rc==SALTS_ENOMEM)status=PRAKTOR_RUNTIME_RESOURCE;
        goto done;
    }
    task_count=praktor_workflow_snapshot_task_count(owner.snapshot);
    praktor_session_config_init(&defaults);
    const char *defaults_error=NULL;
    rc=praktor_yaml_workflow_resolve_defaults(
        praktor_workflow_snapshot_metadata(owner.snapshot),getenv("PRAKTOR_TIMEOUT_MS"),
        getenv("PRAKTOR_STREAM_OUTPUT"),&defaults.shell_timeout_ms,
        &defaults.shell_stream_output,&defaults_error);
    if(rc!=SALTS_OK) {
        if(defaults_error)(void)snprintf(out->error,sizeof out->error,"%s",defaults_error);
        goto done;
    }
    rc=praktor_workflow_task_registry_create(owner.snapshot,&owner.registry);
    if(rc!=SALTS_OK)goto done;
    const json_value_t *parallelism=json_object_get(
        praktor_workflow_snapshot_metadata(owner.snapshot),"parallelism");
    const size_t width=parallelism?(size_t)json_number(parallelism):1u;
    rc=praktor_workflow_dag_state_create_task(
         praktor_workflow_snapshot_dependencies(owner.snapshot),width,selected_task,&owner.dag);
    if(rc!=SALTS_OK) {
        if(rc==SALTS_ENOMEM)status=PRAKTOR_RUNTIME_RESOURCE;
        else if(selected_task)(void)snprintf(out->error,sizeof out->error,"unknown task '%s'",selected_task);
        goto done;
    }
    rc=validate_selected_task_fields(owner.snapshot,owner.dag,out->error,sizeof out->error);
    if(rc!=SALTS_OK) {
        if(rc==SALTS_ENOTSUP)status=PRAKTOR_RUNTIME_UNSUPPORTED;
        goto done;
    }
    rc=praktor_component_host_start(&components,&options,
        praktor_workflow_snapshot_metadata(owner.snapshot),out->error,sizeof out->error);
    if(rc!=SALTS_OK) {
        status=rc==SALTS_ENOMEM?PRAKTOR_RUNTIME_RESOURCE:PRAKTOR_RUNTIME_INVALID;
        if(!out->error[0])(void)snprintf(out->error,sizeof(out->error),
            "CMeta Configurator refused the workflow services");goto done;
    }
    const praktor_yaml_node workflow_declaration=praktor_workflow_snapshot_statechart(owner.snapshot);
    if(praktor_yaml_valid(workflow_declaration)) {
        rc=praktor_statechart_plan_create(workflow_declaration,&defaults.tree,event_poll!=NULL,
            &workflow_plan,out->error,sizeof out->error);
        if(rc!=SALTS_OK) {
            status=rc==SALTS_ENOTSUP?PRAKTOR_RUNTIME_UNSUPPORTED:
                   rc==SALTS_ENOMEM?PRAKTOR_RUNTIME_RESOURCE:PRAKTOR_RUNTIME_INVALID;
            goto done;
        }
    }
    bool has_chart_scope=workflow_plan!=NULL;
    /* Admit every selected action before starting even the first subprocess. */
    for(size_t i=0u;i<task_count;++i){
        bool selected=false;
        if(praktor_workflow_dag_is_selected(owner.dag,i,&selected)!=SALTS_OK)goto done;
        if(!selected) {
            rc=praktor_workflow_task_registry_skip(owner.registry,i);
            if(rc!=SALTS_OK)goto done;
            continue;
        }
        const praktor_yaml_node declaration=praktor_workflow_snapshot_task_statechart(owner.snapshot,i);
        const bool has_chart=praktor_yaml_valid(declaration);
        if(has_chart||workflow_plan) {
            const json_value_t *vars=json_object_get(praktor_workflow_snapshot_task_metadata(owner.snapshot,i),"vars");
            for(size_t j=0u;j<json_object_size(vars);++j) {
                const char *key=json_object_key(vars,j);
                if(!strncmp(key,"chart.",6u)||(has_chart&&!strncmp(key,"event.",6u))) {
                    (void)snprintf(out->error,sizeof out->error,"statechart field '%s' is read-only",key);
                    goto done;
                }
            }
        }
        if(has_chart) {
            has_chart_scope=true;
            rc=praktor_statechart_plan_create(declaration,
                &defaults.tree,event_poll!=NULL,&charts[i],out->error,sizeof out->error);
            if(rc!=SALTS_OK) {
                status=rc==SALTS_ENOTSUP?PRAKTOR_RUNTIME_UNSUPPORTED:
                       rc==SALTS_ENOMEM?PRAKTOR_RUNTIME_RESOURCE:PRAKTOR_RUNTIME_INVALID;
                goto done;
            }
        }
        const praktor_orch_node *root=praktor_orch_tree_root(
            praktor_workflow_snapshot_task_tree(owner.snapshot,i));
        praktor_workflow_snapshot_task_kind kind;
        rc=praktor_workflow_snapshot_task_kind_at(owner.snapshot,i,&kind);
        if(rc!=SALTS_OK)goto done;
        rc=bind_outputs(root,kind==PRAKTOR_WORKFLOW_SNAPSHOT_COMMAND,
            praktor_workflow_snapshot_task_metadata(owner.snapshot,i),(size_t)options.value_capacity,&outputs[i],
            out->error,sizeof(out->error));
        if(rc!=SALTS_OK)goto done;
        rc=praktor_statechart_validate_raises(charts[i],workflow_plan,root,out->error,sizeof out->error);
        if(rc!=SALTS_OK) {
            if(rc==SALTS_ENOMEM)status=PRAKTOR_RUNTIME_RESOURCE;
            goto done;
        }
        rc=make_plan(owner.snapshot,i,has_chart||workflow_plan,&plans[i],out->error,sizeof(out->error));
        if(rc!=SALTS_OK){
            status=PRAKTOR_RUNTIME_UNSUPPORTED;
            if(!out->error[0])(void)snprintf(out->error,sizeof(out->error),
                "Native C11 action is not implemented");goto done;
        }
        rc=bind_tools(&components,plans[i],&tools[i],out->error,sizeof(out->error));
        if(rc!=SALTS_OK) {
            status=rc==SALTS_ENOMEM?PRAKTOR_RUNTIME_RESOURCE:PRAKTOR_RUNTIME_INVALID;
            goto done;
        }
    }
    if(has_chart_scope) {
        const json_value_t *variables=json_object_get(praktor_workflow_snapshot_metadata(owner.snapshot),"variables");
        for(size_t i=0u;i<json_object_size(variables);++i) {
            const char *key=json_object_key(variables,i);
            if(!strncmp(key,"chart.",6u)||!strncmp(key,"event.",6u)) {
                (void)snprintf(out->error,sizeof out->error,"statechart field '%s' is read-only",key);
                goto done;
            }
        }
    }
    rc=praktor_value_store_init(&owner.root,
        (size_t)options.value_capacity,128u,PRK_MAX_VALUE);
    if(rc!=PRAKTOR_VALUE_OK)goto done;
    (void)cmeta_cleanup_arm(&cleanup[3],clean_root,NULL,&owner.root);
    rc=praktor_workflow_seed_root_text_variables(
        owner.snapshot,&owner.root);
    if(rc!=PRAKTOR_VALUE_OK)goto done;
    if(workflow_plan) {
        praktor_native_host_config chart_config;
        praktor_native_host_config_init(&chart_config,1u,128u,PRK_MAX_VALUE);
        chart_config.parent_values=&owner.root;
        chart_config.reserve_event_fields=true;
        chart_config.chart_active=chart_active;
        chart_config.chart_user=&workflow_views;
        praktor_session_config config=defaults;
        config.has_capability=praktor_component_host_has_capability;
        config.capability_user=&components;
        rc=praktor_native_host_init_with_config(&workflow_host,&chart_config);
        if(rc==SALTS_OK)rc=praktor_native_host_bind(&workflow_host,&config.host);
        if(rc==SALTS_OK)rc=praktor_statechart_init(&workflow_chart,workflow_plan,&workflow_host,&config);
        if(rc!=SALTS_OK) {
            status=PRAKTOR_RUNTIME_RESOURCE;
            goto done;
        }
        rc=praktor_statechart_advance(&workflow_chart);
        if(rc!=SALTS_OK) {
            status=PRAKTOR_RUNTIME_FAILED;
            (void)snprintf(out->error,sizeof out->error,"%s",praktor_statechart_error(&workflow_chart));
            goto done;
        }
    }
    rc=praktor_process_retainer_init(&owner.retainer,width);
    if(rc!=SALTS_OK)goto done;
    owner.has_retainer=true;
    const prk_cohort_context ctx={
        .owner=&owner,.components=&components,.workflow_chart=&workflow_chart,
        .defaults=&defaults,.plans=plans,.charts=charts,.tools=tools,.outputs=outputs,
        .value_capacity=(size_t)options.value_capacity,.cancel=cancel,
        .cancel_context=cancel_context,.deadline=deadline,
        .event_poll=event_poll,.event_context=event_context
    };
    status=PRAKTOR_RUNTIME_OK;
    while(status==PRAKTOR_RUNTIME_OK) {
        if(cancel&&cancel(cancel_context)) {
            (void)praktor_workflow_dag_stop(owner.dag,PRAKTOR_WORKFLOW_DAG_CANCELLED);
            status=PRAKTOR_RUNTIME_CANCELLED;
            break;
        }
        if(hrtime()>=deadline) {
            (void)praktor_workflow_dag_stop(owner.dag,PRAKTOR_WORKFLOW_DAG_DEADLINE);
            status=PRAKTOR_RUNTIME_DEADLINE;
            break;
        }
        size_t ids[PRAKTOR_WORKFLOW_MAX_PARALLELISM],count=0u;
        rc=praktor_workflow_dag_claim_ready(owner.dag,ids,width,&count);
        if(rc!=SALTS_OK) {
            status=PRAKTOR_RUNTIME_RESOURCE;
            break;
        }
        if(!count)break;
        status=run_cohort(&ctx,ids,count,out->error,sizeof(out->error));
    }
    if(marshal_result(&owner,out,status)!=SALTS_OK)
        status=PRAKTOR_RUNTIME_RESOURCE;
done:
    if(praktor_statechart_destroy(&workflow_chart)!=SALTS_OK)abort();
    praktor_native_host_destroy(&workflow_host);
    praktor_statechart_plan_destroy(workflow_plan);
    for(size_t i=0u;i<task_count;++i) {
        for(size_t j=0u;j<tools[i].count;++j)
            json_free(tools[i].bindings[j].inputs);
        free(tools[i].bindings);
        praktor_orch_session_plan_destroy(plans[i]);
        praktor_statechart_plan_destroy(charts[i]);
    }
    if(owner.has_retainer) {
        if(drain(&owner.retainer,UINT64_C(5000000000))!=SALTS_OK ||
           praktor_process_retainer_destroy(&owner.retainer)!=SALTS_OK)
            abort(); /* Process ownership is never discarded silently. */
    }
    if(praktor_component_host_stop(&components)!=SALTS_OK)
        status=PRAKTOR_RUNTIME_RESOURCE;
    cmeta_cleanup_reverse(cleanup,4u);
    out->status=status;
    if(status<0 && !out->error[0])
        (void)snprintf(out->error,sizeof(out->error),
            "C11 workflow was not admitted or did not settle");
    return status;
}

praktor_runtime_status praktor_runtime_execute_yaml(
    const char *yaml,size_t size,const praktor_runtime_limits *limits,
    praktor_cancel_probe cancel,void *context,praktor_runtime_result *out) {
    return execute_yaml(yaml,size,limits,cancel,context,NULL,NULL,NULL,out);
}
praktor_runtime_status praktor_runtime_execute_task_yaml(
    const char *yaml,size_t size,const praktor_runtime_limits *limits,
    praktor_cancel_probe cancel,void *context,const char *task,praktor_runtime_result *out) {
    if(!task||!*task)return PRAKTOR_RUNTIME_INVALID;
    return execute_yaml(yaml,size,limits,cancel,context,task,NULL,NULL,out);
}
praktor_runtime_status praktor_runtime_execute_events_yaml(
    const praktor_execute_request *request,const char *task,praktor_event_poll poll,
    void *event_context,praktor_runtime_result *out) {
    if(!request||!out||!poll||(task&&!*task))return PRAKTOR_RUNTIME_INVALID;
    return execute_yaml(request->yaml,request->yaml_size,&request->limits,
        request->is_cancelled,request->cancel_context,task,poll,event_context,out);
}
