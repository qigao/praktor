#include "workflow_snapshot_values.h"
#include <json_parser.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int praktor_c11_workflow_seed_root_text_variables(
    const praktor_workflow_snapshot *snapshot,
    praktor_c11_value_store *store) {
    const json_value_t *metadata,*variables;
    praktor_c11_value_write *writes=NULL;
    size_t count,i;
    int result=PRAKTOR_C11_VALUE_EINVAL;
    if(!snapshot||!store)return result;
    metadata=praktor_workflow_snapshot_metadata(snapshot);
    if(!metadata||json_type(metadata)!=JSON_OBJECT)return result;
    variables=json_object_get(metadata,"variables");
    if(!variables)
        return praktor_c11_value_store_set_many(store,NULL,0u);
    if(json_type(variables)!=JSON_OBJECT)return result;
    count=json_object_size(variables);
    if(count>PRAKTOR_C11_WORKFLOW_MAX_ROOT_VARIABLES||
       count>SIZE_MAX/sizeof(*writes))
        return PRAKTOR_C11_VALUE_ENOSPC;
    if(!count)
        return praktor_c11_value_store_set_many(store,NULL,0u);
    writes=(praktor_c11_value_write *)calloc(count,sizeof(*writes));
    if(!writes)return PRAKTOR_C11_VALUE_ENOMEM;
    for(i=0;i<count;++i) {
        const char *key=json_object_key(variables,i);
        const json_value_t *entry=json_object_value(variables,i);
        const char *value=entry?json_string(entry):NULL;
        const size_t key_len=json_object_key_len(variables,i);
        const size_t value_len=entry?json_string_len(entry):0u;
        /* value_store owns zero-terminated text only. Do not silently turn
         * binary or structured values into truncated strings or JSON blobs. */
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
