#include "yml/yaml_task_metadata.h"
#include "yaml_diagnostic_internal.h"
#include <fmt.h>
#include <string.h>
static int at_error(praktor_yaml_node n,const char *m,praktor_yaml_field_error *e){return praktor_yaml_field_fail_at(n,vstr_from_cstr(m),e);}
static int add_owned(json_value_t *o,const char *k,json_value_t *v){if(!v)return SALTS_ENOMEM;if(!json_object_add_n(o,k,strlen(k),v)){json_free(v);return SALTS_ENOMEM;}return SALTS_OK;}
static int optional(praktor_yaml_node n,const char *k,praktor_yaml_node *out,praktor_yaml_field_error *e){return praktor_yaml_field_get(n,k,out,e);}
static int add_scalar(json_value_t *o,const char *k,praktor_yaml_node n,const char *msg,praktor_yaml_field_error *e){
 tstr s=NULL;int rc=praktor_yaml_field_scalar(n,vstr_from_cstr(msg),&s,e);if(rc!=SALTS_OK)return rc;rc=add_owned(o,k,json_create_string(s));tstr_free(s);return rc;
}
static int add_map(json_value_t *o,const char *k,praktor_yaml_node n,praktor_yaml_field_error *e){
 praktor_yaml_task_fields *owner=NULL;const praktor_yaml_string_map *map=NULL;json_value_t *v=NULL;size_t i;
 int rc=praktor_yaml_task_fields_create(n,PRAKTOR_YAML_STRING_MAP,&owner,e);if(rc!=SALTS_OK)return rc;
 rc=praktor_yaml_task_map(owner,&map);if(rc!=SALTS_OK)goto done;v=json_create_object();if(!v){rc=SALTS_ENOMEM;goto done;}
 for(i=0;i<praktor_yaml_string_map_size(map);++i){praktor_yaml_string_entry ent={0};json_value_t *s;rc=praktor_yaml_string_map_at(map,i,&ent);if(rc!=SALTS_OK)goto done;s=json_create_string_n(ent.value.data,ent.value.len);if(!s){rc=SALTS_ENOMEM;goto done;}if(!json_object_add_n(v,ent.key.data,ent.key.len,s)){json_free(s);rc=SALTS_ENOMEM;goto done;}}
 rc=add_owned(o,k,v);v=NULL;
done: json_free(v);praktor_yaml_task_fields_destroy(owner);if(rc==SALTS_ENOMEM&&e&&e->origin==PRAKTOR_YAML_FAILURE_NONE)e->origin=PRAKTOR_YAML_FAILURE_ALLOCATION;return rc;
}
static int add_list(json_value_t *o,const char *k,praktor_yaml_node n,praktor_yaml_field_error *e){
 praktor_yaml_task_fields *owner=NULL;const praktor_yaml_string_list *list=NULL;json_value_t *v=NULL;size_t i;
 int rc=praktor_yaml_task_fields_create(n,PRAKTOR_YAML_STRING_LIST,&owner,e);if(rc!=SALTS_OK)return rc;
 rc=praktor_yaml_task_list(owner,&list);if(rc!=SALTS_OK)goto done;v=json_create_array();if(!v){rc=SALTS_ENOMEM;goto done;}
 for(i=0;i<praktor_yaml_string_list_size(list);++i){vstr s={0};json_value_t *item;rc=praktor_yaml_string_list_at(list,i,&s);if(rc!=SALTS_OK)goto done;item=json_create_string_n(s.data,s.len);if(!item){rc=SALTS_ENOMEM;goto done;}if(!json_array_add_checked(v,item)){json_free(item);rc=SALTS_ENOMEM;goto done;}}
 rc=add_owned(o,k,v);v=NULL;
done: json_free(v);praktor_yaml_task_fields_destroy(owner);if(rc==SALTS_ENOMEM&&e&&e->origin==PRAKTOR_YAML_FAILURE_NONE)e->origin=PRAKTOR_YAML_FAILURE_ALLOCATION;return rc;
}
int praktor_yaml_task_metadata_create(praktor_yaml_node node,json_value_t **out,praktor_yaml_field_error *error){
 static const vstr keys[]={
 {"name",4},{"description",11},{"depends_on",10},{"vars",4},{"env",3},{"dotEnv",6},{"when",4},{"each",4},{"timeout",7},{"retries",7},{"triggers",8},{"working_dir",11},{"silent",6},{"sources",7},{"generates",9},{"finally",7},{"command",7},{"program",7},{"args",4},{"stdin",5},{"download",8},{"uses",4},{"dynamic_tasks",13},{"output_format",13},{"service",7},{"managed_process",15},{"tool",4},{"with",4},{"script",6},{"actions",7},{"sequence",8},{"parallel",8},{"reactive_sequence",17},{"pipeline_sequence",17},{"inverter",8},{"force_success",13},{"force_failure",13},{"repeat",6},{"delay",5},{"switch",6},{"if",2},{"then",4},{"else",4},{"while",5},{"do",2},{"max_iterations",14},{"child",5},{"cases",5},{"shell",5},{"parse_json",10},{"parse_regex",11},{"parse_lines",11},{"parse_keyvalue",14},{"check_exit_code",15},{"wait_event",10},{"file_exists",11},{"sleep",5},{"set_variable",12},{"subtree",7},{"fallback",8},{"selector",8},{"run_once",8},{"keep_running_until_failure",26},{"consume_queue",13},{"precondition",12},{"entry_updated",13}};
 static const vstr extended[]={
 {"name",4},{"description",11},{"depends_on",10},{"vars",4},{"env",3},{"dotEnv",6},{"when",4},{"each",4},{"timeout",7},{"retries",7},{"triggers",8},{"working_dir",11},{"silent",6},{"sources",7},{"generates",9},{"finally",7},{"command",7},{"program",7},{"args",4},{"stdin",5},{"download",8},{"uses",4},{"dynamic_tasks",13},{"output_format",13},{"service",7},{"managed_process",15},{"tool",4},{"with",4},{"script",6},{"actions",7},{"sequence",8},{"parallel",8},{"reactive_sequence",17},{"pipeline_sequence",17},{"inverter",8},{"force_success",13},{"force_failure",13},{"repeat",6},{"delay",5},{"switch",6},{"if",2},{"then",4},{"else",4},{"while",5},{"do",2},{"max_iterations",14},{"child",5},{"cases",5},{"shell",5},{"parse_json",10},{"parse_regex",11},{"parse_lines",11},{"parse_keyvalue",14},{"check_exit_code",15},{"wait_event",10},{"file_exists",11},{"sleep",5},{"set_variable",12},{"subtree",7},{"fallback",8},{"selector",8},{"run_once",8},{"keep_running_until_failure",26},{"consume_queue",13},{"precondition",12},{"entry_updated",13},{"value",5},{"from",4}};
 praktor_yaml_node n={0},name={0},desc={0},list={0},map={0},when={0},work={0},fin={0},with={0};
 json_value_t *result=NULL;tstr name_text=NULL;int rc;bool extras;
 praktor_yaml_field_error_dispose(error);if(!out)return SALTS_EINVAL;
 if(!praktor_yaml_is_map(node))return at_error(node,"task must be a map",error);
 extras=praktor_yaml_has(node,"set_variable")&&(praktor_yaml_has(node,"value")||praktor_yaml_has(node,"from"));
 rc=praktor_yaml_check_keys(node,extras?extended:keys,extras?sizeof(extended)/sizeof(extended[0]):sizeof(keys)/sizeof(keys[0]),error);if(rc!=SALTS_OK)return rc;
 if(praktor_yaml_has(node,"with")&&!praktor_yaml_has(node,"tool")){rc=optional(node,"with",&with,error);if(rc!=SALTS_OK)return rc;return at_error(with,"'with' is valid only with the 'tool' runner",error);}
 result=json_create_object();if(!result){if(error)error->origin=PRAKTOR_YAML_FAILURE_ALLOCATION;return SALTS_ENOMEM;}
 rc=optional(node,"name",&name,error);if(rc!=SALTS_OK)goto fail;if(!praktor_yaml_valid(name)){rc=at_error(node,"task requires a 'name'",error);goto fail;}
 rc=praktor_yaml_field_scalar(name,vstr_from_cstr("Expected YAML scalar"),&name_text,error);if(rc!=SALTS_OK)goto fail;
 if(!tstr_len(name_text)){rc=at_error(name,"task name cannot be empty",error);goto fail;}
 rc=add_owned(result,"name",json_create_string(name_text));tstr_free(name_text);name_text=NULL;if(rc!=SALTS_OK)goto fail;
 rc=optional(node,"description",&desc,error);if(rc!=SALTS_OK)goto fail;if(praktor_yaml_valid(desc)){rc=add_scalar(result,"description",desc,"Expected YAML scalar",error);if(rc!=SALTS_OK)goto fail;}
 rc=optional(node,"depends_on",&list,error);if(rc!=SALTS_OK)goto fail;if(praktor_yaml_valid(list)){rc=add_list(result,"depends_on",list,error);if(rc!=SALTS_OK)goto fail;}
 rc=optional(node,"vars",&map,error);if(rc!=SALTS_OK)goto fail;if(praktor_yaml_valid(map)){rc=add_map(result,"vars",map,error);if(rc!=SALTS_OK)goto fail;}
 rc=optional(node,"env",&map,error);if(rc!=SALTS_OK)goto fail;if(praktor_yaml_valid(map)){rc=add_map(result,"env",map,error);if(rc!=SALTS_OK)goto fail;}
 rc=optional(node,"dotEnv",&list,error);if(rc!=SALTS_OK)goto fail;if(praktor_yaml_valid(list)){rc=add_list(result,"dot_env",list,error);if(rc!=SALTS_OK)goto fail;}
 rc=optional(node,"when",&when,error);if(rc!=SALTS_OK)goto fail;if(praktor_yaml_valid(when)){rc=add_scalar(result,"when",when,"Expected YAML scalar",error);if(rc!=SALTS_OK)goto fail;}
 rc=optional(node,"working_dir",&work,error);if(rc!=SALTS_OK)goto fail;if(praktor_yaml_valid(work)){tstr s=NULL;rc=praktor_yaml_field_scalar(work,vstr_from_cstr("Expected YAML scalar"),&s,error);if(rc!=SALTS_OK)goto fail;if(tstr_len(s))rc=add_owned(result,"working_dir",json_create_string(s));tstr_free(s);if(rc!=SALTS_OK)goto fail;}
 rc=optional(node,"sources",&list,error);if(rc!=SALTS_OK)goto fail;if(praktor_yaml_valid(list)){rc=add_list(result,"sources",list,error);if(rc!=SALTS_OK)goto fail;}
 rc=optional(node,"generates",&list,error);if(rc!=SALTS_OK)goto fail;if(praktor_yaml_valid(list)){rc=add_list(result,"generates",list,error);if(rc!=SALTS_OK)goto fail;}
 rc=optional(node,"finally",&fin,error);if(rc!=SALTS_OK)goto fail;if(praktor_yaml_valid(fin)){tstr s=NULL;rc=praktor_yaml_field_scalar(fin,vstr_from_cstr("'finally' must be a scalar"),&s,error);if(rc!=SALTS_OK)goto fail;if(!tstr_len(s)){tstr_free(s);rc=at_error(fin,"'finally' cannot be empty",error);goto fail;}rc=add_owned(result,"finally",json_create_string(s));tstr_free(s);if(rc!=SALTS_OK)goto fail;}
 *out=result;return SALTS_OK;
fail:tstr_free(name_text);json_free(result);if(rc==SALTS_ENOMEM&&error&&error->origin==PRAKTOR_YAML_FAILURE_NONE)error->origin=PRAKTOR_YAML_FAILURE_ALLOCATION;return rc;
}
