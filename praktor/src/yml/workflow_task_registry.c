#include "yml/workflow_task_registry.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct task_slot {
    char *name;
    size_t name_len;
    praktor_workflow_task_state state;
    json_value_t *outputs;
    char *reason;
    size_t reason_len;
} task_slot;
struct praktor_workflow_task_registry {
    task_slot *slots;
    size_t count;
};

static task_slot *slot_for(praktor_workflow_task_registry *owner,size_t index) {
    return owner&&index<owner->count?&owner->slots[index]:NULL;
}
static const task_slot *const_slot(const praktor_workflow_task_registry *owner,
                                  size_t index) {
    return owner&&index<owner->count?&owner->slots[index]:NULL;
}
static bool finalized(praktor_workflow_task_state state) {
    return state==PRAKTOR_WORKFLOW_TASK_SUCCESS||
           state==PRAKTOR_WORKFLOW_TASK_FAILED||
           state==PRAKTOR_WORKFLOW_TASK_SKIPPED;
}
/* Bound recursion BEFORE using canonical JsonParser::json_clone(), so caller
 * values cannot force unbounded JSON clone depth. Budget is logical retained
 * bytes, not arena allocator overhead; the latter is separately sanitized.
 * No global state and no allocations during preflight. */
typedef struct json_budget {
    size_t nodes,bytes;
} json_budget;
static bool budget_bytes(json_budget *budget,size_t size) {
    if(size>PRAKTOR_WORKFLOW_TASK_MAX_JSON_BYTES-budget->bytes)return false;
    budget->bytes+=size;
    return true;
}
static bool admissible_json(const json_value_t *node,unsigned depth,
                            json_budget *budget) {
    size_t i;
    const json_type_t kind=node?json_type(node):JSON_NULL;
    if(!node||depth>PRAKTOR_WORKFLOW_TASK_MAX_JSON_DEPTH||
       ++budget->nodes>PRAKTOR_WORKFLOW_TASK_MAX_JSON_NODES)return false;
    switch(kind) {
    case JSON_NULL:
    case JSON_BOOL:
        return budget_bytes(budget,1u);
    case JSON_NUMBER: {
        size_t length=0;
        const char *token=json_number_text(node,&length);
        return budget_bytes(budget,token?length:32u);
    }
    case JSON_STRING:
        return budget_bytes(budget,json_string_len(node));
    case JSON_ARRAY:
        for(i=0;i<json_array_size(node);++i)
            if(!admissible_json(json_array_get(node,i),depth+1u,budget))
                return false;
        return true;
    case JSON_OBJECT:
        for(i=0;i<json_object_size(node);++i) {
            const char *key=json_object_key(node,i);
            const size_t size=json_object_key_len(node,i);
            if(!key||!budget_bytes(budget,size)||
               !admissible_json(json_object_value(node,i),depth+1u,budget))
                return false;
        }
        return true;
    default:return false;
    }
}
void praktor_workflow_task_registry_destroy(
    praktor_workflow_task_registry *owner) {
    size_t i;
    if(!owner)return;
    /* A failed constructor may have published task_count before allocating
     * its slot array. Destruction must be valid for every provisional state. */
    if(owner->slots) {
        for(i=0;i<owner->count;++i) {
            free(owner->slots[i].name);
            free(owner->slots[i].reason);
            json_free(owner->slots[i].outputs);
        }
    }
    free(owner->slots);
    free(owner);
}
int praktor_workflow_task_registry_create(
    const praktor_workflow_snapshot *snapshot,
    praktor_workflow_task_registry **out) {
    const praktor_workflow_dependencies *graph;
    praktor_workflow_task_registry *owner=NULL;
    size_t count,i,bytes=0;
    int status=SALTS_EINVAL;
    if(!snapshot||!out)return status;
    graph=praktor_workflow_snapshot_dependencies(snapshot);
    count=praktor_workflow_dependencies_task_count(graph);
    if(!count||count>PRAKTOR_WORKFLOW_MAX_TASKS||
       count>SIZE_MAX/sizeof(*owner->slots))return status;
    owner=(praktor_workflow_task_registry *)calloc(1,sizeof(*owner));
    if(!owner)return SALTS_ENOMEM;
    owner->count=count;
    owner->slots=(task_slot *)calloc(count,sizeof(*owner->slots));
    if(!owner->slots){status=SALTS_ENOMEM;goto failed;}
    for(i=0;i<count;++i) {
        task_slot *entry=&owner->slots[i];
        vstr name={0};
        status=praktor_workflow_dependencies_task_name(graph,i,&name);
        if(status!=SALTS_OK)goto failed;
        if(!vstr_is_valid(name)||!name.len||
           name.len+1u>PRAKTOR_WORKFLOW_MAX_NAME_BYTES-bytes) {
            status=SALTS_EINVAL;
            goto failed;
        }
        entry->name=(char *)malloc(name.len+1u);
        if(!entry->name){status=SALTS_ENOMEM;goto failed;}
        memcpy(entry->name,name.data,name.len);
        entry->name[name.len]='\0';
        entry->name_len=name.len;
        bytes+=name.len+1u;
        entry->state=PRAKTOR_WORKFLOW_TASK_PENDING;
    }
    *out=owner;
    return SALTS_OK;
failed:
    praktor_workflow_task_registry_destroy(owner);
    return status;
}
size_t praktor_workflow_task_registry_count(
    const praktor_workflow_task_registry *owner) {
    return owner?owner->count:0u;
}
int praktor_workflow_task_registry_name(
    const praktor_workflow_task_registry *owner,size_t index,vstr *out) {
    const task_slot *slot=const_slot(owner,index);
    if(!owner||!out)return SALTS_EINVAL;
    if(!slot)return SALTS_ENOENT;
    *out=vstr_from_buf(slot->name,slot->name_len);
    return SALTS_OK;
}
int praktor_workflow_task_registry_state(
    const praktor_workflow_task_registry *owner,size_t index,
    praktor_workflow_task_state *out) {
    const task_slot *slot=const_slot(owner,index);
    if(!owner||!out)return SALTS_EINVAL;
    if(!slot)return SALTS_ENOENT;
    *out=slot->state;
    return SALTS_OK;
}
int praktor_workflow_task_registry_start(
    praktor_workflow_task_registry *owner,size_t index) {
    task_slot *slot=slot_for(owner,index);
    if(!owner)return SALTS_EINVAL;
    if(!slot)return SALTS_ENOENT;
    json_free(slot->outputs);slot->outputs=NULL;
    free(slot->reason);slot->reason=NULL;slot->reason_len=0u;
    slot->state=PRAKTOR_WORKFLOW_TASK_RUNNING;
    return SALTS_OK;
}
int praktor_workflow_task_registry_complete(
    praktor_workflow_task_registry *owner,size_t index) {
    task_slot *slot=slot_for(owner,index);
    if(!owner)return SALTS_EINVAL;
    if(!slot)return SALTS_ENOENT;
    if(finalized(slot->state))return SALTS_EBUSY;
    slot->state=PRAKTOR_WORKFLOW_TASK_SUCCESS;
    return SALTS_OK;
}
int praktor_workflow_task_registry_fail(
    praktor_workflow_task_registry *owner,size_t index,vstr reason) {
    task_slot *slot=slot_for(owner,index);
    char *copy;
    if(!owner||!vstr_is_valid(reason))return SALTS_EINVAL;
    if(!slot)return SALTS_ENOENT;
    if(finalized(slot->state))return SALTS_EBUSY;
    if(reason.len>PRAKTOR_WORKFLOW_TASK_MAX_REASON_BYTES||
       memchr(reason.data,'\0',reason.len)!=NULL)return SALTS_EINVAL;
    copy=(char *)malloc(reason.len+1u);
    if(!copy)return SALTS_ENOMEM;
    memcpy(copy,reason.data,reason.len);
    copy[reason.len]='\0';
    free(slot->reason);
    slot->reason=copy;
    slot->reason_len=reason.len;
    slot->state=PRAKTOR_WORKFLOW_TASK_FAILED;
    return SALTS_OK;
}
int praktor_workflow_task_registry_skip(
    praktor_workflow_task_registry *owner,size_t index) {
    task_slot *slot=slot_for(owner,index);
    if(!owner)return SALTS_EINVAL;
    if(!slot)return SALTS_ENOENT;
    if(finalized(slot->state))return SALTS_EBUSY;
    slot->state=PRAKTOR_WORKFLOW_TASK_SKIPPED;
    return SALTS_OK;
}
int praktor_workflow_task_registry_set_output(
    praktor_workflow_task_registry *owner,size_t index,
    vstr key,const json_value_t *value) {
    task_slot *slot=slot_for(owner,index);
    json_value_t *staged=NULL,*copy=NULL;
    json_budget budget={0};
    int result=SALTS_EINVAL;
    if(!owner||!vstr_is_valid(key)||!key.len||!value)return result;
    if(!slot)return SALTS_ENOENT;
    if(finalized(slot->state))return SALTS_EBUSY;
    if(key.len>PRAKTOR_WORKFLOW_TASK_MAX_JSON_BYTES||
       memchr(key.data,'\0',key.len)!=NULL)return result;
    if(!admissible_json(value,1u,&budget))return result;
    staged=slot->outputs?json_clone(slot->outputs):json_create_object();
    if(!staged)return SALTS_ENOMEM;
    copy=json_clone(value);
    if(!copy){result=SALTS_ENOMEM;goto cleanup;}
    if(!json_object_add_n(staged,key.data,key.len,copy)) {
        result=SALTS_ENOMEM;
        goto cleanup;
    }
    copy=NULL; /* transferred to staged */
    if(json_object_size(staged)>PRAKTOR_WORKFLOW_TASK_MAX_OUTPUTS)goto cleanup;
    budget=(json_budget){0};
    if(!admissible_json(staged,1u,&budget))goto cleanup;
    json_free(slot->outputs);
    slot->outputs=staged;
    if(slot->state==PRAKTOR_WORKFLOW_TASK_PENDING)
        slot->state=PRAKTOR_WORKFLOW_TASK_RUNNING;
    return SALTS_OK;
cleanup:
    json_free(copy);
    json_free(staged);
    return result;
}
int praktor_workflow_task_registry_merge_outputs(
    praktor_workflow_task_registry *owner,size_t index,
    const json_value_t *updates) {
    task_slot *slot=slot_for(owner,index);
    json_value_t *staged=NULL;
    json_budget budget={0};
    size_t i;
    int status=SALTS_EINVAL;
    if(!owner||!updates||json_type(updates)!=JSON_OBJECT)return status;
    if(!slot)return SALTS_ENOENT;
    if(finalized(slot->state))return SALTS_EBUSY;
    if(!admissible_json(updates,1u,&budget))return status;
    if(json_object_size(updates)>PRAKTOR_WORKFLOW_TASK_MAX_OUTPUTS)
        return status;
    if(json_object_size(updates)==0u)return SALTS_OK;
    staged=slot->outputs?json_clone(slot->outputs):json_create_object();
    if(!staged)return SALTS_ENOMEM;
    for(i=0;i<json_object_size(updates);++i) {
        const char *key=json_object_key(updates,i);
        const size_t length=json_object_key_len(updates,i);
        const json_value_t *value=json_object_value(updates,i);
        json_value_t *copy=NULL;
        if(!key||!length||memchr(key,'\0',length)!=NULL)goto done;
        copy=json_clone(value);
        if(!copy) {
            status=SALTS_ENOMEM;
            goto done;
        }
        if(!json_object_add_n(staged,key,length,copy)) {
            json_free(copy);
            status=SALTS_ENOMEM;
            goto done;
        }
        /* Success transfers this clone to the provisional JSON owner. */
    }
    if(json_object_size(staged)>PRAKTOR_WORKFLOW_TASK_MAX_OUTPUTS)goto done;
    budget=(json_budget){0};
    if(!admissible_json(staged,1u,&budget))goto done;
    json_free(slot->outputs);
    slot->outputs=staged;
    if(slot->state==PRAKTOR_WORKFLOW_TASK_PENDING)
        slot->state=PRAKTOR_WORKFLOW_TASK_RUNNING;
    return SALTS_OK;
done:
    json_free(staged);
    return status;
}
int praktor_workflow_task_registry_get_output(
    const praktor_workflow_task_registry *owner,size_t index,
    vstr key,const json_value_t **out) {
    const task_slot *slot=const_slot(owner,index);
    const json_value_t *value;
    if(!owner||!out||!vstr_is_valid(key)||!key.len)return SALTS_EINVAL;
    if(!slot)return SALTS_ENOENT;
    if(slot->state!=PRAKTOR_WORKFLOW_TASK_SUCCESS&&
       slot->state!=PRAKTOR_WORKFLOW_TASK_FAILED)return SALTS_EBUSY;
    value=slot->outputs?json_object_get_v(slot->outputs,key):NULL;
    if(!value)return SALTS_ENOENT;
    *out=value;
    return SALTS_OK;
}
int praktor_workflow_task_registry_get_outputs(
    const praktor_workflow_task_registry *owner,size_t index,
    const json_value_t **out) {
    const task_slot *slot=const_slot(owner,index);
    if(!owner||!out)return SALTS_EINVAL;
    if(!slot)return SALTS_ENOENT;
    if(slot->state!=PRAKTOR_WORKFLOW_TASK_SUCCESS&&
       slot->state!=PRAKTOR_WORKFLOW_TASK_FAILED)return SALTS_EBUSY;
    if(!slot->outputs)return SALTS_ENOENT;
    *out=slot->outputs;
    return SALTS_OK;
}
int praktor_workflow_task_registry_failure_reason(
    const praktor_workflow_task_registry *owner,size_t index,vstr *out) {
    const task_slot *slot=const_slot(owner,index);
    if(!owner||!out)return SALTS_EINVAL;
    if(!slot)return SALTS_ENOENT;
    if(slot->state!=PRAKTOR_WORKFLOW_TASK_FAILED)return SALTS_ENOENT;
    *out=vstr_from_buf(slot->reason,slot->reason_len);
    return SALTS_OK;
}
