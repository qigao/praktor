/* Native Orch AST -> C11 Session parity qualification. No C++ bridge,
 * implicit Legacy action fallback, dynamic scheduler, or external Process. */
#include "orch_session_plan.h"
#include "native_host.h"
#include "yml/workflow_snapshot.h"
#include <salts/error_codes.h>
#include <stdint.h>
#include <string.h>
#include <tinytest.h>

static const char *native_workflow=
    "name: Native C11 Session Plan\n"
    "tasks:\n"
    "  - name: calculate\n"
    "    actions:\n"
    "      Sequence:\n"
    "        - set_variable: answer\n"
    "          value: '7'\n"
    "        - CheckExitCode: {input_key: answer, expected: 7}\n";

static const char *delayed_workflow=
    "tasks:\n"
    "  - name: delayed\n"
    "    actions:\n"
    "      Sequence:\n"
    "        - Sleep: {duration: 1s}\n"
    "        - set_variable: late\n"
    "          value: visible\n";

static praktor_workflow_snapshot *snapshot(const char *yaml) {
    praktor_workflow_snapshot *owner=NULL;
    char reason[256]={0};
    const int status=praktor_workflow_snapshot_create_inline(
        vstr_from_cstr(yaml),&owner,reason,sizeof reason);
    if(status!=SALTS_OK)info("Native Orch Snapshot: %s",reason);
    check(status==SALTS_OK);
    return owner;
}
static bool value_is(const praktor_c11_value_store *store,
                     const char *key,const char *expected) {
    char text[128]={0};
    size_t length=0u;
    return praktor_c11_value_store_get(store,key,text,sizeof text,&length)==
        PRAKTOR_C11_VALUE_OK&&length==strlen(expected)&&
        strcmp(text,expected)==0;
}
static uint64_t fake_time(void *user) {
    return *(const uint64_t *)user;
}

spec("C11 Orch AST to owned Session Plan and native leaf execution") {
    it("compiles actual YAML AST, survives source teardown, executes SetVariable/CheckExitCode") {
        praktor_workflow_snapshot *input=snapshot(native_workflow);
        praktor_c11_orch_session_plan *plan=NULL;
        praktor_c11_native_host host={0};
        praktor_c11_bt_host hooks={0};
        praktor_c11_session session={0};
        praktor_c11_session_config config={0};
        salts_bt_node_status outcome=SALTS_BT_RUNNING;
        char reason[256]={0};
        if(!input)return;
        check(praktor_c11_native_host_init(&host,8u,64u,128u)==SALTS_OK);
        check(praktor_c11_native_host_bind(&host,&hooks)==SALTS_OK);
        praktor_c11_session_config_init(&config);
        config.host=hooks;
        check(praktor_c11_orch_session_plan_create(
            praktor_workflow_snapshot_task_tree(input,0u),&config.tree,
            &plan,reason,sizeof reason)==SALTS_OK);
        if(!plan) {
            info("Session Plan error: %s",reason);
            praktor_workflow_snapshot_destroy(input);
            praktor_c11_native_host_destroy(&host);
            return;
        }
        check(praktor_c11_orch_session_plan_node_count(plan)==3u);
        /* The plan copies strings and topology; the Session copies the plan. */
        praktor_workflow_snapshot_destroy(input);
        check(praktor_c11_orch_session_plan_init_session(
            plan,&config,&session)==SALTS_OK);
        praktor_c11_orch_session_plan_destroy(plan);
        check(praktor_c11_session_tick(&session,&outcome)==SALTS_OK);
        check(outcome==SALTS_BT_SUCCESS);
        check(value_is(praktor_c11_native_host_values(&host),"answer","7"));
        check(praktor_c11_session_destroy(&session)==SALTS_OK);
        praktor_c11_native_host_destroy(&host);
    }

    it("cancels a real running C11 Sleep before any downstream SetVariable effect") {
        praktor_workflow_snapshot *input=snapshot(delayed_workflow);
        praktor_c11_orch_session_plan *plan=NULL;
        praktor_c11_native_host host={0};
        praktor_c11_native_host_config host_config;
        praktor_c11_bt_host hooks={0};
        praktor_c11_session session={0};
        praktor_c11_session_config config={0};
        salts_bt_node_status outcome=SALTS_BT_FAILURE;
        uint64_t now=0u;
        char error[256]={0};
        if(!input)return;
        praktor_c11_native_host_config_init(&host_config,8u,64u,128u);
        host_config.max_running_leaves=1u;
        host_config.now_ns=fake_time;
        host_config.clock_user=&now;
        check(praktor_c11_native_host_init_with_config(
            &host,&host_config)==SALTS_OK);
        check(praktor_c11_native_host_bind(&host,&hooks)==SALTS_OK);
        check(praktor_c11_value_store_set(
            praktor_c11_native_host_values(&host),"late","original")==
            PRAKTOR_C11_VALUE_OK);
        praktor_c11_session_config_init(&config);
        config.host=hooks;
        check(praktor_c11_orch_session_plan_create(
            praktor_workflow_snapshot_task_tree(input,0u),&config.tree,
            &plan,error,sizeof error)==SALTS_OK);
        if(!plan) {
            info("Sleep plan: %s",error);
            praktor_workflow_snapshot_destroy(input);
            praktor_c11_native_host_destroy(&host);
            return;
        }
        praktor_workflow_snapshot_destroy(input);
        check(praktor_c11_orch_session_plan_init_session(
            plan,&config,&session)==SALTS_OK);
        praktor_c11_orch_session_plan_destroy(plan);
        check(praktor_c11_session_tick(&session,&outcome)==SALTS_OK);
        check(outcome==SALTS_BT_RUNNING);
        check(value_is(praktor_c11_native_host_values(&host),"late","original"));
        check(praktor_c11_session_cancel(&session)==SALTS_OK);
        check(value_is(praktor_c11_native_host_values(&host),"late","original"));
        check(praktor_c11_session_destroy(&session)==SALTS_OK);
        praktor_c11_native_host_destroy(&host);
    }

    it("refuses Shell, embedded NUL and Mustache without modifying a valid plan") {
        praktor_workflow_snapshot *input=snapshot(native_workflow);
        praktor_c11_orch_session_plan *saved=NULL,*out;
        praktor_orch_tree *shell=NULL,*binary=NULL;
        praktor_orch_command command={0};
        praktor_c11_session_config config={0};
        const char encoded[]={'a','\0','b'};
        praktor_orch_input_param params[2]={
            {vstr_from_cstr("key"),vstr_from_cstr("target")},
            {vstr_from_cstr("value"),vstr_from_buf(encoded,sizeof encoded)}
        };
        praktor_orch_flat_node leaf={
            PRAKTOR_ORCH_SET_VARIABLE,PRAKTOR_ORCH_ROOT_PARENT,params,2u
        };
        char error[256]={0};
        if(!input)return;
        praktor_c11_session_config_init(&config);
        check(praktor_c11_orch_session_plan_create(
            praktor_workflow_snapshot_task_tree(input,0u),&config.tree,
            &saved,error,sizeof error)==SALTS_OK);
        check(saved!=NULL);
        if(!saved) {
            praktor_workflow_snapshot_destroy(input);
            return;
        }
        command.command=vstr_from_cstr("echo do-not-run");
        check(praktor_orch_from_command(&command,&shell,NULL,0u)==SALTS_OK);
        out=saved;
        check(praktor_c11_orch_session_plan_create(
            shell,&config.tree,&out,error,sizeof error)==SALTS_ENOTSUP);
        check(out==saved);
        check(strstr(error,"no Legacy fallback")!=NULL);
        check(praktor_orch_from_flat_nodes(
            &leaf,1u,&binary,NULL,0u)==SALTS_OK);
        out=saved;
        check(praktor_c11_orch_session_plan_create(
            binary,&config.tree,&out,error,sizeof error)==SALTS_ENOTSUP);
        check(out==saved);
        check(strstr(error,"Binary Orch parameter")!=NULL);
        check(praktor_c11_orch_session_plan_node_count(saved)==3u);
        {
            praktor_orch_tree *template_tree=NULL;
            praktor_orch_input_param template_params[2]={
                {vstr_from_cstr("key"),vstr_from_cstr("target")},
                {vstr_from_cstr("value"),vstr_from_cstr("{{ answer }}")}
            };
            praktor_orch_flat_node template_leaf={
                PRAKTOR_ORCH_SET_VARIABLE,PRAKTOR_ORCH_ROOT_PARENT,
                template_params,2u
            };
            check(praktor_orch_from_flat_nodes(
                &template_leaf,1u,&template_tree,NULL,0u)==SALTS_OK);
            out=saved;
            check(praktor_c11_orch_session_plan_create(
                template_tree,&config.tree,&out,error,sizeof error)==SALTS_ENOTSUP);
            check(out==saved);
            check(strstr(error,"Mustache expansion")!=NULL);
            praktor_orch_tree_destroy(template_tree);
        }
        praktor_orch_tree_destroy(binary);
        praktor_orch_tree_destroy(shell);
        praktor_c11_orch_session_plan_destroy(saved);
        praktor_workflow_snapshot_destroy(input);
    }

    it("rejects retired named SetVariable YAML instead of reviving Legacy syntax") {
        static const char *legacy=
            "tasks:\n"
            "  - name: bad\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - SetVariable: {key: answer, value: 7}\n"
            "        - CheckExitCode: {input_key: answer, expected: 7}\n";
        praktor_workflow_snapshot *input=NULL;
        char error[256]={0};
        check(praktor_workflow_snapshot_create_inline(
            vstr_from_cstr(legacy),&input,error,sizeof error)!=SALTS_OK);
        check(input==NULL);
        check(strstr(error,"Legacy orchestration node type")!=NULL);
    }

    it("rejects noncanonical stdout bindings for command KeyValue without losing prior plan") {
        praktor_workflow_snapshot *input=snapshot(native_workflow);
        praktor_c11_orch_session_plan *saved=NULL,*out;
        praktor_c11_session_config config={0};
        char error[256]={0};
        const praktor_orch_input_param shell_ports[]={
            {vstr_from_cstr("cmd"),vstr_from_cstr("echo KEY=42")},
            {vstr_from_cstr("output_key"),vstr_from_cstr("not_stdout")}
        };
        const praktor_orch_input_param parse_ports[]={
            {vstr_from_cstr("input_key"),vstr_from_cstr("stdout")},
            {vstr_from_cstr("output_key"),vstr_from_cstr("parsed")}
        };
        const praktor_orch_flat_node nodes[]={
            {PRAKTOR_ORCH_SEQUENCE,PRAKTOR_ORCH_ROOT_PARENT,NULL,0u},
            {PRAKTOR_ORCH_SHELL,0u,shell_ports,2u},
            {PRAKTOR_ORCH_PARSE_KEY_VALUE,0u,parse_ports,2u}
        };
        praktor_orch_tree *tree=NULL;
        if(!input)return;
        praktor_c11_session_config_init(&config);
        check(praktor_c11_orch_session_plan_create(
            praktor_workflow_snapshot_task_tree(input,0u),&config.tree,
            &saved,error,sizeof error)==SALTS_OK);
        if(!saved){praktor_workflow_snapshot_destroy(input);return;}
        check(praktor_orch_from_flat_nodes(
            nodes,3u,&tree,NULL,0u)==SALTS_OK);
        out=saved;
        check(praktor_c11_orch_session_plan_create_native_command_keyvalue(
            tree,&config.tree,&out,error,sizeof error)==SALTS_ENOTSUP);
        check(out==saved);
        check(strstr(error,"canonical stdout port binding")!=NULL);
        praktor_orch_tree_destroy(tree);
        check(praktor_c11_orch_session_plan_node_count(saved)==3u);
        praktor_c11_orch_session_plan_destroy(saved);
        praktor_workflow_snapshot_destroy(input);
    }

    it("refuses too-small tree limits with no partial output owner") {
        praktor_workflow_snapshot *input=snapshot(native_workflow);
        praktor_c11_session_config config={0};
        praktor_c11_orch_session_plan *plan=NULL;
        char error[128]={0};
        if(!input)return;
        praktor_c11_session_config_init(&config);
        config.tree.max_nodes=2u;
        check(praktor_c11_orch_session_plan_create(
            praktor_workflow_snapshot_task_tree(input,0u),&config.tree,
            &plan,error,sizeof error)==SALTS_EINVAL);
        check(plan==NULL);
        check(strstr(error,"limits")!=NULL);
        praktor_workflow_snapshot_destroy(input);
    }
}
