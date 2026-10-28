/* C11-only Praktor owner. Dynamic Plugin lease ownership is reserved for
 * ServiceHost; the current static provider is held by Configurator. */
#include "praktor/runtime.h"
#include "component_host.h"
#include "yml/workflow_snapshot.h"
#include "yml/workflow_dag_state.h"
#include "yml/workflow_task_registry.h"
#include "orch_session_plan.h"
#include "native_host.h"
#include "workflow_snapshot_values.h"
#include "workflow_parallel_process_guard.h"
#include <cflow/executor.h>
#include <cmeta/ace_interceptor.h>
#include <cmeta/cleanup.h>
#include <salts/process_retainer.h>
#include <salts/clock.h>
#include <salts/error_codes.h>
#include <json_parser.h>
#include <vstr.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRK_MAX_TASKS 64u
#define PRK_MAX_VALUE 4096u
#define PRK_MAX_OUTPUTS 3u

typedef struct prk_state {
    praktor_workflow_snapshot *snapshot;
    praktor_workflow_task_registry *registry;
    praktor_workflow_dag_state *dag;
    praktor_c11_value_store root;
    cflow_process_retainer retainer;
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
    praktor_c11_value_store_destroy((praktor_c11_value_store *)slot);
}
void praktor_runtime_result_destroy(praktor_runtime_result *result) {
    if(!result)return;
    if(result->json)json_serialize_free(result->json);
    *result=(praktor_runtime_result){0};
}
static int drain(cflow_process_retainer *retainer,uint64_t budget_ns) {
    uint64_t started=cmeta_hrtime();
    size_t pending=SIZE_MAX;
    do {
        size_t reaped=0u;
        int status=cflow_process_retainer_run_ready(
            retainer,64u,&reaped,&pending);
        if(status!=SALTS_OK&&status!=SALTS_EBUSY)return status;
        if(status==SALTS_OK&&pending==0u)return SALTS_OK;
        cmeta_sleep_ms(1u);
    }while(cmeta_hrtime()-started<budget_ns);
    return SALTS_EBUSY;
}
/* CFlow callback owns the mutable Session for the whole tick run; no CFlow
 * worker touches the DAG, root variable store or TaskRegistry. */
typedef struct prk_callback {
    praktor_c11_session *session;
    praktor_cancel_probe cancel;
    void *cancel_context;
    uint64_t deadline;
    int status;
    salts_bt_node_status outcome;
    bool cancelled,expired,ran,finalized;
} prk_callback;
static void callback_run(void *p) {
    prk_callback *c=(prk_callback *)p;
    c->ran=true;
    c->status=SALTS_OK;
    c->outcome=SALTS_BT_RUNNING;
    while(c->outcome==SALTS_BT_RUNNING) {
        if(c->cancel&&c->cancel(c->cancel_context)) {
            c->cancelled=true;
            c->status=praktor_c11_session_cancel(c->session);
            break;
        }
        if(cmeta_hrtime()>=c->deadline) {
            c->expired=true;
            c->status=praktor_c11_session_cancel(c->session);
            break;
        }
        c->status=praktor_c11_session_tick(c->session,&c->outcome);
        if(c->status!=SALTS_OK)break;
        if(c->outcome==SALTS_BT_RUNNING)cmeta_sleep_ms(1u);
    }
}
static void callback_cancel(void *p) {
    ((prk_callback *)p)->cancelled=true;
}
static void callback_finalize(void *p) {
    ((prk_callback *)p)->finalized=true;
}

typedef struct prk_admission {
    praktor_workflow_dag_state *dag;
    size_t task_id;
    cflow_executor *executor;
    cflow_executor_task descriptor;
} prk_admission;
typedef struct prk_ack { bool accepted; } prk_ack;
CMETA_INTERCEPTOR_TYPE(prk_action_gate,prk_admission,prk_ack);
static cmeta_status before_admit(void *ctx,const prk_admission *request,
                                  bool *proceed) {
    const praktor_component_host *component=(const praktor_component_host *)ctx;
    bool reserved=false;
    if(!component||!component->active||component->admitted!=1 ||
       praktor_workflow_dag_is_reserved(request->dag,request->task_id,
                                        &reserved)!=SALTS_OK)
        return CMETA_CALLBACK_ERROR;
    *proceed=reserved;
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
static int make_plan(const praktor_workflow_snapshot *snapshot,size_t id,
    praktor_c11_orch_session_plan **out,char *error,size_t capacity) {
    praktor_workflow_snapshot_task_kind kind;
    praktor_c11_session_config cfg={0};
    const praktor_orch_tree *tree=praktor_workflow_snapshot_task_tree(snapshot,id);
    const praktor_orch_node *node=praktor_orch_tree_root(tree);
    if(!node||praktor_workflow_snapshot_task_kind_at(snapshot,id,&kind)!=SALTS_OK)
        return SALTS_EINVAL;
    praktor_c11_session_config_init(&cfg);
    if(kind==PRAKTOR_WORKFLOW_SNAPSHOT_COMMAND)
        return praktor_c11_orch_session_plan_create_native_command(
            tree,&cfg.tree,out,error,capacity);
    switch(praktor_orch_node_type(node)) {
    case PRAKTOR_ORCH_SHELL:
        return praktor_c11_orch_session_plan_create_native_shell(
            tree,&cfg.tree,out,error,capacity);
    case PRAKTOR_ORCH_SET_VARIABLE:
        return praktor_c11_orch_session_plan_create(
            tree,&cfg.tree,out,error,capacity);
    default:
        return SALTS_ENOTSUP;
    }
}
static int native_set_key(const praktor_orch_tree *tree,
                          char *out,size_t capacity) {
    const praktor_orch_node *node=praktor_orch_tree_root(tree);
    if(!node||praktor_orch_node_type(node)!=PRAKTOR_ORCH_SET_VARIABLE)
        return SALTS_ENOTSUP;
    for(size_t i=0u;i<praktor_orch_param_count(node);++i) {
        praktor_orch_param param={0};
        if(praktor_orch_param_at(node,i,&param)!=SALTS_OK)return SALTS_EINVAL;
        if(vstr_eq(param.key,vstr_from_cstr("key"))) {
            if(!param.value.len||param.value.len>=capacity ||
               memchr(param.value.data,'\0',param.value.len))return SALTS_EINVAL;
            memcpy(out,param.value.data,param.value.len);
            out[param.value.len]='\0';
            return SALTS_OK;
        }
    }
    return SALTS_EINVAL;
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
static int read_output(praktor_c11_value_store *store,const char *key,
                       char *data,size_t capacity,size_t *length) {
    if(praktor_c11_value_store_get(store,key,data,capacity,length)!=
            PRAKTOR_C11_VALUE_OK)return SALTS_ENOENT;
    if(*length>=capacity||memchr(data,'\0',*length))return SALTS_EINVAL;
    data[*length]='\0';
    return SALTS_OK;
}
static int publish_success(prk_state *s,size_t task_id,
    praktor_c11_value_store *scope,bool shell,const char *set_key,
    cflow_executor_control *control) {
    const char *ports[PRK_MAX_OUTPUTS]={0};
    const char *labels[PRK_MAX_OUTPUTS]={0};
    size_t count=0u;
    char values[PRK_MAX_OUTPUTS][PRK_MAX_VALUE+1u]={{0}};
    size_t lengths[PRK_MAX_OUTPUTS]={0};
    praktor_c11_value_txn txn={0};
    json_value_t *typed=json_create_object();
    praktor_workflow_task_output_update update={0};
    int rc=SALTS_EINVAL;
    if(!typed)return SALTS_ENOMEM;
    if(shell) {
        ports[0]="shell_exit_code";labels[0]="exit_code";
        ports[1]="shell_output";labels[1]="stdout";
        ports[2]="shell_stderr";labels[2]="stderr";count=3u;
    }else {
        ports[0]=set_key;labels[0]=set_key;count=1u;
    }
    for(size_t i=0u;i<count;++i) {
        if(!ports[i]||read_output(scope,ports[i],values[i],
                sizeof(values[i]),&lengths[i])!=SALTS_OK ||
            !add_owned(typed,labels[i],
                json_create_string_n(values[i],lengths[i])))
            goto done;
    }
    if(praktor_c11_workflow_prepare_task_text_outputs(
            &s->root,scope,ports,count,&txn)!=PRAKTOR_C11_VALUE_OK)goto done;
    update.task_index=task_id;update.updates=typed;
    size_t pending=SIZE_MAX;
    uint64_t started=cmeta_hrtime();
    do {
        int commit=praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
            s->registry,&update,&txn,1u,control,&s->retainer,64u,&pending);
        if(commit==PRAKTOR_C11_PARALLEL_OK && pending==0u) {
            rc=SALTS_OK;break;
        }
        if(commit!=PRAKTOR_C11_PARALLEL_BUSY)break;
        cmeta_sleep_ms(1u);
    }while(cmeta_hrtime()-started<UINT64_C(5000000000));
done:
    praktor_c11_value_txn_abort(&txn);
    json_free(typed);
    return rc;
}
static int publish_failure_exit(prk_state *s,size_t index,
                                praktor_c11_value_store *scope) {
    char code[64]={0};size_t n=0u;
    if(read_output(scope,"shell_exit_code",code,sizeof(code),&n)!=SALTS_OK)
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
praktor_runtime_status praktor_runtime_execute_yaml(
    const char *yaml,size_t yaml_size,const praktor_runtime_limits *specified,
    praktor_cancel_probe cancel,void *cancel_context,
    praktor_runtime_result *out) {
    praktor_runtime_limits options=
        specified?*specified:praktor_runtime_limits_default();
    praktor_component_host components={0};
    prk_state owner={0};
    cmeta_cleanup cleanup[4]={{0}};
    praktor_c11_orch_session_plan *plans[PRK_MAX_TASKS]={0};
    praktor_runtime_status status=PRAKTOR_RUNTIME_INVALID;
    size_t task_count=0u;
    uint64_t deadline=0u;
    int rc=SALTS_OK;
    bool component_live=false;
    if(!out||out->json||!yaml||!yaml_size||yaml_size>1024u*1024u ||
       options.max_tasks<1||options.max_tasks>(int)PRK_MAX_TASKS ||
       options.value_capacity<1||options.value_capacity>4096||
       options.timeout_ms<1)return PRAKTOR_RUNTIME_INVALID;
    out->status=PRAKTOR_RUNTIME_INVALID;
    out->error[0]='\0';
    if(praktor_component_host_start(&components,&options)!=SALTS_OK){
        (void)snprintf(out->error,sizeof(out->error),
            "CMeta Configurator refused the C11 owner");goto done;
    }
    component_live=true;
    deadline=cmeta_hrtime()+(uint64_t)options.timeout_ms*UINT64_C(1000000);
    (void)cmeta_cleanup_arm(&cleanup[0],clean_snapshot,NULL,&owner.snapshot);
    (void)cmeta_cleanup_arm(&cleanup[1],clean_registry,NULL,&owner.registry);
    (void)cmeta_cleanup_arm(&cleanup[2],clean_dag,NULL,&owner.dag);
    rc=praktor_workflow_snapshot_create_inline(vstr_from_buf(yaml,yaml_size),
         &owner.snapshot,out->error,sizeof(out->error));
    if(rc!=SALTS_OK)goto done;
    task_count=praktor_workflow_snapshot_task_count(owner.snapshot);
    if(task_count>(size_t)options.max_tasks){
        (void)snprintf(out->error,sizeof(out->error),
            "C11 workflow exceeds configured task capacity");goto done;
    }
    rc=praktor_workflow_task_registry_create(owner.snapshot,&owner.registry);
    if(rc!=SALTS_OK)goto done;
    rc=praktor_workflow_dag_state_create(
        praktor_workflow_snapshot_dependencies(owner.snapshot),1u,&owner.dag);
    if(rc!=SALTS_OK)goto done;
    /* Admit EVERY action before starting even the first OS subprocess. */
    for(size_t i=0u;i<task_count;++i){
        rc=make_plan(owner.snapshot,i,&plans[i],out->error,sizeof(out->error));
        if(rc!=SALTS_OK){
            status=PRAKTOR_RUNTIME_UNSUPPORTED;
            if(!out->error[0])(void)snprintf(out->error,sizeof(out->error),
                "Native C11 action is not implemented");goto done;
        }
    }
    rc=praktor_c11_value_store_init(&owner.root,
        (size_t)options.value_capacity,128u,PRK_MAX_VALUE);
    if(rc!=PRAKTOR_C11_VALUE_OK)goto done;
    (void)cmeta_cleanup_arm(&cleanup[3],clean_root,NULL,&owner.root);
    rc=praktor_c11_workflow_seed_root_text_variables(
        owner.snapshot,&owner.root);
    if(rc!=PRAKTOR_C11_VALUE_OK)goto done;
    rc=cflow_process_retainer_init(&owner.retainer,1u);
    if(rc!=SALTS_OK)goto done;
    owner.has_retainer=true;
    status=PRAKTOR_RUNTIME_OK;
    for(;;){
        size_t index=SIZE_MAX,claims=0u;
        if(cancel&&cancel(cancel_context)){
            (void)praktor_workflow_dag_stop(
                owner.dag,PRAKTOR_WORKFLOW_DAG_CANCELLED);
            status=PRAKTOR_RUNTIME_CANCELLED;break;
        }
        if(cmeta_hrtime()>=deadline){
            (void)praktor_workflow_dag_stop(
                owner.dag,PRAKTOR_WORKFLOW_DAG_DEADLINE);
            status=PRAKTOR_RUNTIME_DEADLINE;break;
        }
        rc=praktor_workflow_dag_claim_ready(
            owner.dag,&index,1u,&claims);
        if(rc==SALTS_EBUSY||(rc==SALTS_OK&&!claims))break;
        if(rc!=SALTS_OK||index>=task_count){
            status=PRAKTOR_RUNTIME_RESOURCE;break;
        }
        praktor_c11_native_host host={0};
        praktor_c11_bt_host hooks={0};
        praktor_c11_native_host_config hostconfig={0};
        praktor_c11_session session={0};
        praktor_c11_session_config config={0};
        cflow_executor executor={0};
        cflow_executor_control control={0};
        prk_callback driver={0};
        bool has_host=false,has_session=false,has_executor=false;
        bool accepted=false,passed=false;
        bool shell=false;
        char selected_key[128]={0};
        int task_error=SALTS_OK;
        praktor_workflow_snapshot_task_kind kind;
        const praktor_orch_tree *tree=
            praktor_workflow_snapshot_task_tree(owner.snapshot,index);
        const praktor_orch_node *root=praktor_orch_tree_root(tree);
        shell=praktor_orch_node_type(root)==PRAKTOR_ORCH_SHELL;
        if(praktor_workflow_snapshot_task_kind_at(
                owner.snapshot,index,&kind)!=SALTS_OK){
            task_error=SALTS_EINVAL;goto task_done;
        }
        shell=shell||kind==PRAKTOR_WORKFLOW_SNAPSHOT_COMMAND;
        task_error=praktor_workflow_task_registry_start(owner.registry,index);
        if(task_error!=SALTS_OK)goto task_done;
        praktor_c11_native_host_config_init(&hostconfig,
            (size_t)options.value_capacity,128u,PRK_MAX_VALUE);
        hostconfig.max_running_leaves=1u;
        task_error=praktor_c11_native_host_init_with_config(
            &host,&hostconfig);
        if(task_error!=SALTS_OK)goto task_done;
        has_host=true;
        task_error=praktor_c11_workflow_load_task_text_scope(
            owner.snapshot,index,&owner.root,
            praktor_c11_native_host_values(&host));
        if(task_error!=PRAKTOR_C11_VALUE_OK)goto task_done;
        task_error=praktor_c11_native_host_bind(&host,&hooks);
        if(task_error!=SALTS_OK)goto task_done;
        praktor_c11_session_config_init(&config);
        config.host=hooks;config.retainer=&owner.retainer;
        task_error=praktor_c11_orch_session_plan_init_session(
            plans[index],&config,&session);
        if(task_error!=SALTS_OK)goto task_done;
        has_session=true;
        if(!cflow_executor_manual_init_with_capacity(&executor,1u)) {
            task_error=SALTS_EINVAL;goto task_done;
        }
        has_executor=true;
        if(!cflow_executor_as_control(&executor,&control)) {
            task_error=SALTS_EINVAL;goto task_done;
        }
        driver.session=&session;
        driver.cancel=cancel;driver.cancel_context=cancel_context;
        driver.deadline=deadline;
        cflow_executor_task descriptor={
            callback_run,callback_cancel,callback_finalize,&driver};
        prk_admission admission={owner.dag,index,&executor,descriptor};
        prk_ack ack={false};
        prk_action_gate_hook gate={
            &components,before_admit,NULL,NULL};
        prk_action_gate chain={NULL,admit_action,&gate,1u};
        accepted=prk_action_gate_invoke(&chain,&admission,&ack)==CMETA_OK
                 &&ack.accepted;
        if(!accepted){
            (void)praktor_workflow_dag_refuse(owner.dag,index);
            task_error=SALTS_EBUSY;goto task_done;
        }
        if(cflow_executor_run_ready(&executor)!=1u ||
           !cflow_executor_control_shutdown(
               &control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN) ||
           cflow_executor_control_wait_idle(&control)!=CFLOW_EXECUTOR_WAIT_IDLE ||
           !driver.ran||!driver.finalized){
            task_error=SALTS_EBUSY;goto task_done;
        }
        passed=driver.status==SALTS_OK &&
               driver.outcome==SALTS_BT_SUCCESS &&
               !driver.cancelled&&!driver.expired;
        if(driver.cancelled){
            (void)praktor_workflow_dag_stop(
                owner.dag,PRAKTOR_WORKFLOW_DAG_CANCELLED);
            status=PRAKTOR_RUNTIME_CANCELLED;
        }else if(driver.expired){
            (void)praktor_workflow_dag_stop(
                owner.dag,PRAKTOR_WORKFLOW_DAG_DEADLINE);
            status=PRAKTOR_RUNTIME_DEADLINE;
        }else if(!passed)status=PRAKTOR_RUNTIME_FAILED;
task_done:
        if(has_session){
            int closed=praktor_c11_session_destroy(&session);
            if(closed!=SALTS_OK){
                /* NativeIO has not proved borrowed owner transfer; never
                 * pretend this fallible close is a lexical RAII callback. */
                abort();
            }
            has_session=false;
        }
        /* Keep the borrowed CFlow control alive through atomic publish. */
        if(drain(&owner.retainer,UINT64_C(5000000000))!=SALTS_OK)
            abort(); /* Retainer cannot be invalidated while still borrowed. */
        if(accepted&&task_error==SALTS_OK) {
            if(passed){
                if(!shell)task_error=native_set_key(
                    tree,selected_key,sizeof(selected_key));
                if(task_error==SALTS_OK)task_error=publish_success(
                    &owner,index,praktor_c11_native_host_values(&host),
                    shell,shell?NULL:selected_key,&control);
            }else if(shell) {
                /* Failure exit is a typed task diagnostic, not a root commit. */
                (void)publish_failure_exit(&owner,index,
                    praktor_c11_native_host_values(&host));
            }
        }
        if(has_executor)cflow_executor_destroy(&executor);
        if(has_host)praktor_c11_native_host_destroy(&host);
        if(task_error!=SALTS_OK){
            status=PRAKTOR_RUNTIME_RESOURCE;
            (void)snprintf(out->error,sizeof(out->error),
                "C11 callback/admission failed (status %d)",task_error);
            (void)praktor_workflow_dag_stop(
                owner.dag,PRAKTOR_WORKFLOW_DAG_FAILED);
        }
        if(!passed||task_error!=SALTS_OK) {
            (void)praktor_workflow_task_registry_fail(owner.registry,index,
                vstr_from_cstr("C11 action failed, cancelled or timed out"));
            (void)praktor_workflow_dag_finish(owner.dag,index,false);
            if(status==PRAKTOR_RUNTIME_OK)status=PRAKTOR_RUNTIME_FAILED;
        }else{
            (void)praktor_workflow_task_registry_complete(owner.registry,index);
            (void)praktor_workflow_dag_finish(owner.dag,index,true);
        }
        if(status!=PRAKTOR_RUNTIME_OK)break;
    }
    if(marshal_result(&owner,out,status)!=SALTS_OK)
        status=PRAKTOR_RUNTIME_RESOURCE;
done:
    for(size_t i=0u;i<task_count;++i)
        praktor_c11_orch_session_plan_destroy(plans[i]);
    if(owner.has_retainer) {
        if(drain(&owner.retainer,UINT64_C(5000000000))!=SALTS_OK ||
           cflow_process_retainer_destroy(&owner.retainer)!=SALTS_OK)
            abort(); /* Process ownership is never discarded silently. */
    }
    cmeta_cleanup_reverse(cleanup,4u);
    if(components.active && praktor_component_host_stop(&components)!=SALTS_OK)
        status=PRAKTOR_RUNTIME_RESOURCE;
    out->status=status;
    if(status<0 && !out->error[0])
        (void)snprintf(out->error,sizeof(out->error),
            "C11 workflow was not admitted or did not settle");
    return status;
}
