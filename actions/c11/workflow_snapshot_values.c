#include "workflow_snapshot_values.h"
#include <json_parser.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The canonical metadata constructors already own decoded UTF-8 YAML text.
 * This function only bridges their owned JSON string maps into the EXISTING
 * bounded value store; no second map/index/transaction implementation. */
static int seed_text_object(const json_value_t *map,
                            praktor_c11_value_store *store) {
    praktor_c11_value_write *writes=NULL;
    size_t count,i;
    int result=PRAKTOR_C11_VALUE_EINVAL;
    if(!map)return praktor_c11_value_store_set_many(store,NULL,0u);
    if(json_type(map)!=JSON_OBJECT)return result;
    count=json_object_size(map);
    if(count>PRAKTOR_C11_WORKFLOW_MAX_ROOT_VARIABLES||
       count>SIZE_MAX/sizeof(*writes))
        return PRAKTOR_C11_VALUE_ENOSPC;
    if(!count)return praktor_c11_value_store_set_many(store,NULL,0u);
    writes=(praktor_c11_value_write *)calloc(count,sizeof(*writes));
    if(!writes)return PRAKTOR_C11_VALUE_ENOMEM;
    for(i=0;i<count;++i) {
        const char *key=json_object_key(map,i);
        const json_value_t *entry=json_object_value(map,i);
        const char *value=entry?json_string(entry):NULL;
        const size_t key_len=json_object_key_len(map,i);
        const size_t value_len=entry?json_string_len(entry):0u;
        /* A text store cannot represent binary keys/values or typed JSON
         * values. Refuse the WHOLE batch instead of truncating anything. */
        if(!key||!key_len||!value||
           memchr(key,'\0',key_len)!=NULL||
           memchr(value,'\0',value_len)!=NULL)goto done;
        writes[i].key=key;
        writes[i].value=value;
    }
    result=praktor_c11_value_store_set_many(store,writes,count);
done:
    free(writes);
    return result;
}

int praktor_c11_workflow_seed_root_text_variables(
    const praktor_workflow_snapshot *snapshot,
    praktor_c11_value_store *store) {
    const json_value_t *metadata;
    if(!snapshot||!store)return PRAKTOR_C11_VALUE_EINVAL;
    metadata=praktor_workflow_snapshot_metadata(snapshot);
    if(!metadata||json_type(metadata)!=JSON_OBJECT)
        return PRAKTOR_C11_VALUE_EINVAL;
    return seed_text_object(json_object_get(metadata,"variables"),store);
}

int praktor_c11_workflow_fork_task_text_variables(
    const praktor_workflow_snapshot *snapshot, size_t task_index,
    const praktor_c11_value_store *root,
    praktor_c11_value_store *child) {
    const json_value_t *task;
    praktor_c11_value_store staging={0};
    int result;
    if(!snapshot||!root||!child||root==child||child->impl)
        return PRAKTOR_C11_VALUE_EINVAL;
    task=praktor_workflow_snapshot_task_metadata(snapshot,task_index);
    if(!task||json_type(task)!=JSON_OBJECT)
        return PRAKTOR_C11_VALUE_EINVAL;
    result=praktor_c11_value_store_clone(&staging,root);
    if(result!=PRAKTOR_C11_VALUE_OK)return result;
    result=seed_text_object(json_object_get(task,"vars"),&staging);
    if(result==PRAKTOR_C11_VALUE_OK) {
        *child=staging;
        return PRAKTOR_C11_VALUE_OK;
    }
    praktor_c11_value_store_destroy(&staging);
    return result;
}

/* No NativeHost implementation knowledge: callers pass the pre-existing
 * value_store slot exposed through native_host_values(). The canonical
 * value-store replacement owns all copied task state. */
int praktor_c11_workflow_load_task_text_scope(
    const praktor_workflow_snapshot *snapshot,size_t task_index,
    const praktor_c11_value_store *root,
    praktor_c11_value_store *destination) {
    praktor_c11_value_store staged={0};
    int status;
    if(!snapshot||!root||!destination||root==destination||
       !root->impl||!destination->impl)return PRAKTOR_C11_VALUE_EINVAL;
    status=praktor_c11_workflow_fork_task_text_variables(
        snapshot,task_index,root,&staged);
    if(status!=PRAKTOR_C11_VALUE_OK)return status;
    status=praktor_c11_value_store_replace_clone(destination,&staged);
    praktor_c11_value_store_destroy(&staged);
    return status;
}

/* The only publication set is the explicit list selected by the Owner, not
 * a wholesale merge of root or task-local inherited variables. All writes
 * reside in a canonical optimistic transaction until the Owner commits. */
int praktor_c11_workflow_prepare_task_text_outputs(
    praktor_c11_value_store *root,
    const praktor_c11_value_store *task_scope,
    const char *const *selected_keys,size_t key_count,
    praktor_c11_value_txn *out) {
    praktor_c11_value_txn staged={0};
    size_t retained=0u,i,j;
    int status;
    if(!root||!root->impl||!task_scope||!task_scope->impl||
       root==task_scope||!out||out->impl||!selected_keys||!key_count)
        return PRAKTOR_C11_VALUE_EINVAL;
    if(key_count>PRAKTOR_C11_WORKFLOW_MAX_TEXT_OUTPUTS)
        return PRAKTOR_C11_VALUE_ENOSPC;
    for(i=0u;i<key_count;++i) {
        if(!selected_keys[i]||!selected_keys[i][0])
            return PRAKTOR_C11_VALUE_EINVAL;
        for(j=0u;j<i;++j)
            if(strcmp(selected_keys[i],selected_keys[j])==0)
                return PRAKTOR_C11_VALUE_EINVAL;
    }
    status=praktor_c11_value_txn_begin(&staged,root);
    if(status!=PRAKTOR_C11_VALUE_OK)return status;
    for(i=0u;i<key_count;++i) {
        size_t size=0u,read_size=0u;
        char *copy=NULL;
        status=praktor_c11_value_store_get(
            task_scope,selected_keys[i],NULL,0u,&size);
        if(status!=PRAKTOR_C11_VALUE_ENOBUFS)goto fail;
        if(size>=PRAKTOR_C11_WORKFLOW_MAX_TEXT_OUTPUT_BYTES-retained) {
            status=PRAKTOR_C11_VALUE_ENOBUFS;
            goto fail;
        }
        copy=(char *)calloc(size+1u,1u);
        if(!copy){status=PRAKTOR_C11_VALUE_ENOMEM;goto fail;}
        status=praktor_c11_value_store_get(
            task_scope,selected_keys[i],copy,size+1u,&read_size);
        if(status==PRAKTOR_C11_VALUE_OK&&read_size==size)
            status=praktor_c11_value_txn_set(
                &staged,selected_keys[i],copy);
        else if(status==PRAKTOR_C11_VALUE_OK)
            status=PRAKTOR_C11_VALUE_EINVAL;
        free(copy);
        if(status!=PRAKTOR_C11_VALUE_OK)goto fail;
        retained+=size+1u;
    }
    *out=staged;
    return PRAKTOR_C11_VALUE_OK;
fail:
    praktor_c11_value_txn_abort(&staged);
    return status;
}
