#include "yml/yaml_workflow_metadata.h"
#include "yaml_diagnostic_internal.h"
#include "yml/yaml_task_fields.h"
#include <string.h>
static int at_error(praktor_yaml_node node,const char *message,praktor_yaml_field_error *error) {
    return praktor_yaml_field_fail_at(node,vstr_from_cstr(message),error);
}
static int add_value(json_value_t *object,const char *key,json_value_t *value) {
    if(!value)return SALTS_ENOMEM;
    if(!json_object_add_n(object,key,strlen(key),value)){json_free(value);return SALTS_ENOMEM;}
    return SALTS_OK;
}
static int optional(praktor_yaml_node node,const char *key,praktor_yaml_node *out,praktor_yaml_field_error *error) {
    return praktor_yaml_field_get(node,key,out,error);
}
static int add_scalar(json_value_t *object,const char *key,praktor_yaml_node node,
                      const char *message,praktor_yaml_field_error *error) {
    tstr text=NULL;
    int status=praktor_yaml_field_scalar(node,vstr_from_cstr(message),&text,error);
    if(status!=SALTS_OK)return status;
    status=add_value(object,key,json_create_string(text));tstr_free(text);return status;
}

static int add_string_map(json_value_t *object, const char *key,
                          praktor_yaml_node node, praktor_yaml_field_error *error) {
    praktor_yaml_task_fields *owner = NULL;
    const praktor_yaml_string_map *map = NULL;
    json_value_t *result = NULL;
    size_t index;
    int status = praktor_yaml_task_fields_create(node, PRAKTOR_YAML_STRING_MAP, &owner, error);
    if (status != SALTS_OK) return status;
    status = praktor_yaml_task_map(owner, &map);
    if (status != SALTS_OK) goto done;
    result = json_create_object();
    if (!result) { status = SALTS_ENOMEM; goto done; }
    for (index = 0; index < praktor_yaml_string_map_size(map); ++index) {
        praktor_yaml_string_entry entry = {0};
        json_value_t *value;
        status = praktor_yaml_string_map_at(map, index, &entry);
        if (status != SALTS_OK) goto done;
        value = json_create_string_n(entry.value.data, entry.value.len);
        if (!value) { status = SALTS_ENOMEM; goto done; }
        if (!json_object_add_n(result, entry.key.data, entry.key.len, value)) {
            json_free(value); status = SALTS_ENOMEM; goto done;
        }
    }
    status = add_value(object, key, result);
    result = NULL;
done:
    json_free(result);
    praktor_yaml_task_fields_destroy(owner);
    if (status == SALTS_ENOMEM && error && error->origin == PRAKTOR_YAML_FAILURE_NONE)
        error->origin = PRAKTOR_YAML_FAILURE_ALLOCATION;
    return status;
}

static int add_string_list(json_value_t *object, const char *key,
                           praktor_yaml_node node, praktor_yaml_field_error *error) {
    praktor_yaml_task_fields *owner = NULL;
    const praktor_yaml_string_list *list = NULL;
    json_value_t *result = NULL;
    size_t index;
    int status = praktor_yaml_task_fields_create(node, PRAKTOR_YAML_STRING_LIST, &owner, error);
    if (status != SALTS_OK) return status;
    status = praktor_yaml_task_list(owner, &list);
    if (status != SALTS_OK) goto done;
    result = json_create_array();
    if (!result) { status = SALTS_ENOMEM; goto done; }
    for (index = 0; index < praktor_yaml_string_list_size(list); ++index) {
        vstr text = {0};
        json_value_t *value;
        status = praktor_yaml_string_list_at(list, index, &text);
        if (status != SALTS_OK) goto done;
        value = json_create_string_n(text.data, text.len);
        if (!value) { status = SALTS_ENOMEM; goto done; }
        if (!json_array_add_checked(result, value)) {
            json_free(value); status = SALTS_ENOMEM; goto done;
        }
    }
    status = add_value(object, key, result);
    result = NULL;
done:
    json_free(result);
    praktor_yaml_task_fields_destroy(owner);
    if (status == SALTS_ENOMEM && error && error->origin == PRAKTOR_YAML_FAILURE_NONE)
        error->origin = PRAKTOR_YAML_FAILURE_ALLOCATION;
    return status;
}
int praktor_yaml_workflow_metadata_create(praktor_yaml_node node,json_value_t **out,
    praktor_yaml_field_error *error) {
    static const vstr allowed[]={{"name",4},{"description",11},{"inputs",6},{"outputs",7},
        {"input_policy",12},{"variables",9},{"env",3},{"dotEnv",6},{"defaults",8},
        {"includes",8},{"tasks",5}};
    static const vstr default_keys[]={{"timeout",7}};
    praktor_yaml_node child={0},defaults_node={0},timeout_node={0};
    praktor_yaml_node variables_node={0},env_node={0},dot_env_node={0};
    json_value_t *result=NULL,*defaults=NULL;
    tstr policy=NULL;int status;
    praktor_yaml_field_error_dispose(error);
    if(!out)return SALTS_EINVAL;
    if(!praktor_yaml_is_map(node))return at_error(node,"workflow must be a map",error);
    status=praktor_yaml_check_keys(node,allowed,sizeof(allowed)/sizeof(allowed[0]),error);
    if(status!=SALTS_OK)return status;
    result=json_create_object();
    if(!result){if(error)error->origin=PRAKTOR_YAML_FAILURE_ALLOCATION;return SALTS_ENOMEM;}
    status=optional(node,"name",&child,error);if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(child)){status=add_scalar(result,"name",child,"Expected YAML scalar",error);if(status!=SALTS_OK)goto fail;}
    status=optional(node,"description",&child,error);if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(child)){status=add_scalar(result,"description",child,"Expected YAML scalar",error);if(status!=SALTS_OK)goto fail;}
    status=optional(node,"input_policy",&child,error);if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(child)) {
        status=praktor_yaml_field_scalar(child,vstr_from_cstr("input_policy must be a scalar"),&policy,error);
        if(status!=SALTS_OK)goto fail;
        if(strcmp(policy,"strict")&&strcmp(policy,"allow_extra")) {
            tstr_free(policy);policy=NULL;
            status=at_error(child,"input_policy must be 'strict' or 'allow_extra'",error);goto fail;
        }
        status=add_value(result,"strict_inputs",json_create_bool(strcmp(policy,"strict")==0));
        tstr_free(policy);policy=NULL;if(status!=SALTS_OK)goto fail;
    }
    status=optional(node,"variables",&variables_node,error);if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(variables_node)) {
        status=add_string_map(result,"variables",variables_node,error);
        if(status!=SALTS_OK)goto fail;
    }
    status=optional(node,"env",&env_node,error);if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(env_node)) {
        status=add_string_map(result,"env",env_node,error);
        if(status!=SALTS_OK)goto fail;
    }
    status=optional(node,"dotEnv",&dot_env_node,error);if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(dot_env_node)) {
        status=add_string_list(result,"dot_env",dot_env_node,error);
        if(status!=SALTS_OK)goto fail;
    }
    status=optional(node,"defaults",&defaults_node,error);if(status!=SALTS_OK)goto fail;
    if(praktor_yaml_valid(defaults_node)) {
        if(!praktor_yaml_is_map(defaults_node)){status=at_error(defaults_node,"defaults must be a map",error);goto fail;}
        status=praktor_yaml_check_keys(defaults_node,default_keys,1u,error);if(status!=SALTS_OK)goto fail;
        defaults=json_create_object();if(!defaults){status=SALTS_ENOMEM;goto fail;}
        status=optional(defaults_node,"timeout",&timeout_node,error);if(status!=SALTS_OK)goto fail;
        if(praktor_yaml_valid(timeout_node)){status=add_scalar(defaults,"timeout",timeout_node,"Expected YAML scalar",error);if(status!=SALTS_OK)goto fail;}
        status=add_value(result,"defaults",defaults);defaults=NULL;if(status!=SALTS_OK)goto fail;
    }
    *out=result;return SALTS_OK;
fail:
    tstr_free(policy);json_free(defaults);json_free(result);
    if(status==SALTS_ENOMEM&&error&&error->origin==PRAKTOR_YAML_FAILURE_NONE)error->origin=PRAKTOR_YAML_FAILURE_ALLOCATION;
    return status;
}
