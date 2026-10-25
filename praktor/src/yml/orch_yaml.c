#include "yml/orch_yaml.h"
#include <ctype.h>
#include <salts/error_codes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NAMED_YAML_MAX_DEPTH 100u
typedef struct named_row {
    praktor_orch_flat_node node;
    praktor_orch_input_param params[PRAKTOR_ORCH_MAX_PARAMS];
    tstr keys[PRAKTOR_ORCH_MAX_PARAMS],values[PRAKTOR_ORCH_MAX_PARAMS];
} named_row;
typedef struct named_build {
    named_row *rows;
    size_t count;
    char *error;
    size_t capacity;
} named_build;
static int fail(named_build *b,int status,const char *reason) {
    if(b->error&&b->capacity)(void)snprintf(b->error,b->capacity,"%s",reason);
    return status;
}
static void cleanup(named_build *b) {
    size_t i,j;
    for(i=0;i<b->count;++i)
        for(j=0;j<PRAKTOR_ORCH_MAX_PARAMS;++j) {
            tstr_free(b->rows[i].keys[j]);
            tstr_free(b->rows[i].values[j]);
        }
    free(b->rows);
}
/* Match the legacy parse-error location convention for syntax and validation.
 * Diagnostics are best-effort and never acquire ownership of the YAML DOM. */
static int fail_at(named_build *b,int status,praktor_yaml_node node,const char *message) {
    tstr location=NULL;
    if(b->error&&b->capacity) {
        if(praktor_yaml_location(node,&location)==SALTS_OK)
            (void)snprintf(b->error,b->capacity,"Parse error%s: %s",location,message);
        else (void)snprintf(b->error,b->capacity,"Parse error: %s",message);
    }
    tstr_free(location);
    return status;
}
static bool runtime_scalar(tstr value) {
    size_t i;
    for(i=0;i<tstr_len(value);++i)
        if(value[i]=='{'||value[i]=='}')return true;
    return false;
}
static bool key_is(tstr key,const char *expected) {
    size_t i=0,j=0;
    while(key[i]&&expected[j]) {
        if(key[i]=='_'){++i;continue;}
        if(expected[j]=='_'){++j;continue;}
        if(tolower((unsigned char)key[i])!=tolower((unsigned char)expected[j]))return false;
        ++i;++j;
    }
    while(key[i]=='_')++i;
    while(expected[j]=='_')++j;
    return key[i]=='\0'&&expected[j]=='\0';
}
static const char *canonical_param(const praktor_orch_spec *spec,const char *key) {
    size_t i;
    if(!spec||!key)return NULL;
    for(i=0;i<sizeof(spec->required)/sizeof(spec->required[0]);++i)
        if(spec->required[i]&&key_is((tstr)key,spec->required[i]))return spec->required[i];
    for(i=0;i<sizeof(spec->optional)/sizeof(spec->optional[0]);++i)
        if(spec->optional[i]&&key_is((tstr)key,spec->optional[i]))return spec->optional[i];
    return NULL;
}
static int validate_param(named_build *b,tstr key,tstr value) {
    static const char *const integer_keys[]={"capture_group","expected","timeout",
        "num_attempts","num_cycles","timeout_ms","delay_ms","max_iterations"};
    static const char *const boolean_keys[]={"stream_output","fail_if_missing"};
    size_t i,j,n=tstr_len(value),start=0;
    bool integer=false,boolean=false;
    if(runtime_scalar(value))return SALTS_OK;
    for(i=0;i<sizeof(integer_keys)/sizeof(integer_keys[0]);++i)
        if(key_is(key,integer_keys[i])){integer=true;break;}
    for(i=0;i<sizeof(boolean_keys)/sizeof(boolean_keys[0]);++i)
        if(key_is(key,boolean_keys[i])){boolean=true;break;}
    if(integer) {
        if(n&&(value[0]=='+'||value[0]=='-'))start=1u;
        if(start==n) {
            tstr_free(key);tstr_free(value);
            return fail(b,SALTS_EINVAL,"Orchestration integer parameter must be an integer");
        }
        for(j=start;j<n;++j)if(value[j]<'0'||value[j]>'9') {
            tstr_free(key);tstr_free(value);
            return fail(b,SALTS_EINVAL,"Orchestration integer parameter must be an integer");
        }
    }
    if(boolean&&!(n==4u&&tolower((unsigned char)value[0])=='t'&&
       tolower((unsigned char)value[1])=='r'&&tolower((unsigned char)value[2])=='u'&&
       tolower((unsigned char)value[3])=='e')&&
       !(n==5u&&tolower((unsigned char)value[0])=='f'&&
       tolower((unsigned char)value[1])=='a'&&tolower((unsigned char)value[2])=='l'&&
       tolower((unsigned char)value[3])=='s'&&tolower((unsigned char)value[4])=='e')) {
        tstr_free(key);tstr_free(value);
        return fail(b,SALTS_EINVAL,"Orchestration boolean parameter must be true or false");
    }
    return SALTS_OK;
}
static int param(named_build *b,size_t index,tstr key,tstr value) {
    named_row *row=&b->rows[index];
    size_t p=row->node.param_count,j;
    int validation=validate_param(b,key,value);
    if(validation!=SALTS_OK)return validation;
    /* The legacy map uses assignment into a canonical-key map: the last
     * spelling wins without creating a second parameter entry. Keep the
     * lower-level flat-node constructor's separate strict-duplicate contract. */
    for(j=0;j<p;++j)if(key_is(row->keys[j],key)) {
        tstr_free(row->keys[j]);tstr_free(row->values[j]);
        row->keys[j]=key;row->values[j]=value;
        row->params[j]=(praktor_orch_input_param){tstr_to_v(key),tstr_to_v(value)};
        return SALTS_OK;
    }
    if(p>=PRAKTOR_ORCH_MAX_PARAMS) {
        tstr_free(key);tstr_free(value);
        return fail(b,SALTS_EINVAL,"Named orchestration parameter limit");
    }
    row->keys[p]=key;row->values[p]=value;
    row->params[p]=(praktor_orch_input_param){tstr_to_v(key),tstr_to_v(value)};
    row->node.param_count=p+1u;
    return SALTS_OK;
}
static int parse(named_build *b,praktor_yaml_node map,size_t parent,unsigned depth);
static int flat_branch(named_build *b,praktor_yaml_node branch,size_t parent,unsigned depth);
static int flat_parse(named_build *b,praktor_yaml_node map,size_t parent,unsigned depth,const char *kind,bool allow_metadata);
static int children(named_build *b,praktor_yaml_node seq,size_t parent,unsigned depth) {
    size_t i;
    if(!praktor_yaml_is_seq(seq))
        return fail(b,SALTS_EINVAL,"Named orchestration children must be a sequence");
    for(i=0;i<praktor_yaml_count(seq);++i) {
        int status=parse(b,praktor_yaml_at(seq,i),parent,depth+1u);
        if(status!=SALTS_OK)return status;
    }
    return SALTS_OK;
}
/* Shared named-node admission for recursive YAML and task-level runner values.
 * The task-level caller selects one exact field, not the metadata-bearing map.
 * Never retain the borrowed YAML value/key handle beyond this construction. */
static int parse_named_entry(named_build *b,praktor_yaml_node value,size_t parent,unsigned depth) {
    praktor_orch_type type=PRAKTOR_ORCH_TYPE_COUNT;
    const praktor_orch_spec *spec;
    tstr type_name=NULL;
    size_t index,i;
    int status;
    if(depth>NAMED_YAML_MAX_DEPTH)return fail(b,SALTS_EINVAL,"Named orchestration nesting limit");
    if(b->count>=PRAKTOR_ORCH_MAX_NODES)
        return fail(b,SALTS_EINVAL,"Named orchestration node limit");
    status=praktor_yaml_key(value,&type_name,NULL);
    if(status!=SALTS_OK)return fail(b,status,"Cannot decode orchestration type key");
    /* Legacy named spellings were explicitly retired. The flat forms must
     * remain the sole accepted syntax for these control/decorator nodes. */
    if(key_is(type_name,"IfThenElse")||key_is(type_name,"WhileDo")||
       key_is(type_name,"Switch")||key_is(type_name,"SetVariable")||
       key_is(type_name,"Timeout")||key_is(type_name,"Repeat")||
       key_is(type_name,"Delay")) {
        tstr_free(type_name);
        return fail_at(b,SALTS_EINVAL,value,
            "Legacy orchestration node type has been removed; use flat syntax");
    }
    status=praktor_orch_find_type(tstr_to_v(type_name),&type);
    tstr_free(type_name);
    if(status!=SALTS_OK)return fail_at(b,SALTS_EINVAL,value,"Unknown named orchestration type");
    spec=praktor_orch_spec_for(type);
    index=b->count++;
    b->rows[index].node.type=type;
    b->rows[index].node.parent_index=parent;
    if(praktor_yaml_is_seq(value)) {
        if(!spec->allows_children)return fail(b,SALTS_EINVAL,"Leaf cannot have children");
        return children(b,value,index,depth);
    }
    if(praktor_yaml_is_map(value)) {
        bool has_child=false,has_children=false;
        char type_lower[64];
        size_t t;
        (void)snprintf(type_lower,sizeof type_lower,"%s",spec->name);
        for(t=0;type_lower[t];++t)
            type_lower[t]=(char)tolower((unsigned char)type_lower[t]);
        /* Legacy map-style parsing validates every scalar and canonical key
         * before descending into children.  The order is observable when a
         * node contains both an invalid scalar and an invalid child subtree. */
        for(i=0;i<praktor_yaml_count(value);++i) {
            praktor_yaml_node field=praktor_yaml_at(value,i);
            tstr key=NULL,text=NULL;
            const char *canonical;
            status=praktor_yaml_key(field,&key,NULL);
            if(status!=SALTS_OK)
                return fail_at(b,status,field,"Cannot decode orchestration parameter key");
            if(key_is(key,"child")) {
                has_child=true;tstr_free(key);continue;
            }
            if(key_is(key,"children")) {
                has_children=true;tstr_free(key);continue;
            }
            canonical=canonical_param(spec,key);
            if(!canonical) {
                char message[256];
                (void)snprintf(message,sizeof message,
                    "Unknown orchestration parameter '%s' for node type '%s'",
                    key,type_lower);
                tstr_free(key);
                return fail_at(b,SALTS_EINVAL,field,message);
            }
            status=praktor_yaml_scalar(field,&text,NULL);
            if(status!=SALTS_OK) {
                char message[256];
                (void)snprintf(message,sizeof message,
                    "Orchestration parameter '%s' for node type '%s' must be a scalar",
                    key,type_lower);
                tstr_free(key);
                return fail_at(b,status,field,message);
            }
            status=param(b,index,key,text);
            if(status!=SALTS_OK)return status;
        }
        if(has_child&&has_children)
            return fail_at(b,SALTS_EINVAL,value,
                "Orchestration node cannot define both 'child' and 'children'");
        /* The flags above use the legacy normalized-key test, but subsequent
         * retrieval deliberately uses the same exact raw sibling name as the
         * legacy node["child"]/node["children"] operator. */
        if(has_children||has_child) {
            praktor_yaml_node branch={0};
            const char *name=has_children?"children":"child";
            status=praktor_yaml_get(value,name,&branch,NULL);
            if(status!=SALTS_OK)
                return fail_at(b,SALTS_EINVAL,value,
                    "Orchestration child field is not accessible by its canonical name");
            if(!spec->allows_children) {
                char message[128];
                (void)snprintf(message,sizeof message,
                    "Leaf node '%s' cannot have children",type_lower);
                return fail_at(b,SALTS_EINVAL,branch,message);
            }
            return has_children?children(b,branch,index,depth):
                flat_branch(b,branch,index,depth);
        }
        return SALTS_OK;
    }
    {
        const char *required=NULL;
        tstr key=NULL,text=NULL;
        size_t count=0;
        for(i=0;i<sizeof(spec->required)/sizeof(spec->required[0]);++i)
            if(spec->required[i]) {required=spec->required[i];++count;}
        if(count!=1u)return fail(b,SALTS_EINVAL,"Named shorthand requires one parameter");
        status=praktor_yaml_scalar(value,&text,NULL);
        if(status!=SALTS_OK)return fail(b,status,"Named shorthand must be scalar");
        key=tstr_dup(required);
        if(!key) {tstr_free(text);return fail(b,SALTS_ENOMEM,"Cannot allocate param key");}
        return param(b,index,key,text);
    }
}

/* Recursive actions: a map with one named type, or an established flat
 * sibling form. Task metadata is intentionally *not* admitted in this path. */
static int parse(named_build *b,praktor_yaml_node map,size_t parent,unsigned depth) {
    if(praktor_yaml_is_map(map)) {
        if(praktor_yaml_has(map,"if")&&praktor_yaml_has(map,"then"))
            return flat_parse(b,map,parent,depth,"if",false);
        if(praktor_yaml_has(map,"while")&&praktor_yaml_has(map,"do"))
            return flat_parse(b,map,parent,depth,"while",false);
        if(praktor_yaml_has(map,"switch")&&praktor_yaml_has(map,"cases"))
            return flat_parse(b,map,parent,depth,"switch",false);
        if(praktor_yaml_has(map,"set_variable")&&
           (praktor_yaml_has(map,"value")||praktor_yaml_has(map,"from")))
            return flat_parse(b,map,parent,depth,"set_variable",false);
        {
            static const char *const decorators[]={"timeout","repeat","delay"};
            size_t j;
            for(j=0;j<sizeof(decorators)/sizeof(decorators[0]);++j)
                if(praktor_yaml_has(map,decorators[j])&&praktor_yaml_has(map,"child"))
                    return flat_parse(b,map,parent,depth,decorators[j],false);
        }
    }
    if(depth>NAMED_YAML_MAX_DEPTH)
        return fail(b,SALTS_EINVAL,"Named orchestration nesting limit");
    if(!praktor_yaml_is_map(map)||praktor_yaml_count(map)!=1u)
        return fail(b,SALTS_EINVAL,"Named node requires exactly one type key");
    return parse_named_entry(b,praktor_yaml_at(map,0),parent,depth);
}

static bool flat_key_matches(const char *actual,const char *expected) {
    size_t a=0,e=0;
    if(!actual||!expected)return false;
    for(;;) {
        while(actual[a]=='_')++a;
        while(expected[e]=='_')++e;
        if(!actual[a]||!expected[e])return actual[a]==expected[e];
        if(tolower((unsigned char)actual[a])!=tolower((unsigned char)expected[e]))return false;
        ++a;++e;
    }
}
/* Flat sibling lookups intentionally use the provider's exact raw key
 * semantics, matching legacy node.has_child()/operator[]; normalized aliases
 * are allowed by the whitelist but do not silently become actual siblings. */
static bool flat_find_field(praktor_yaml_node map,const char *name,praktor_yaml_node *out) {
    return out&&praktor_yaml_get(map,name,out,NULL)==SALTS_OK;
}
static bool flat_task_metadata_key(const char *key) {
    static const char *const keys[]={
        "name","description","depends_on","vars","env","dot_env","fallback","selector",
        "when","each","timeout","triggers","working_dir","silent","sources","generates",
        "finally","command","uses","dynamic_tasks","output_format","parse_regex","parse_json",
        "parse_lines","parse_keyvalue","script","actions"
    };
    size_t i;
    for(i=0;i<sizeof(keys)/sizeof(keys[0]);++i)
        if(flat_key_matches(key,keys[i]))return true;
    return false;
}
static int flat_append_node(named_build *b,praktor_orch_type type,size_t parent,
                            unsigned depth,size_t *index) {
    if(depth>NAMED_YAML_MAX_DEPTH)return fail(b,SALTS_EINVAL,"Orchestration nesting limit");
    if(b->count>=PRAKTOR_ORCH_MAX_NODES)return fail(b,SALTS_EINVAL,"Orchestration node limit");
    *index=b->count++;
    b->rows[*index].node.type=type;
    b->rows[*index].node.parent_index=parent;
    return SALTS_OK;
}
static int flat_add_param(named_build *b,size_t index,const char *key,tstr value) {
    tstr owned_key=tstr_dup(key);
    if(!owned_key) {
        tstr_free(value);
        return fail(b,SALTS_ENOMEM,"Cannot allocate orchestration parameter key");
    }
    return param(b,index,owned_key,value);
}
static int flat_branch(named_build *b,praktor_yaml_node branch,size_t parent,unsigned depth) {
    size_t i,index;
    int status;
    if(praktor_yaml_is_seq(branch)) {
        status=flat_append_node(b,PRAKTOR_ORCH_SEQUENCE,parent,depth,&index);
        if(status!=SALTS_OK)return status;
        for(i=0;i<praktor_yaml_count(branch);++i) {
            status=parse(b,praktor_yaml_at(branch,i),index,depth+1u);
            if(status!=SALTS_OK)return status;
        }
        return SALTS_OK;
    }
    return parse(b,branch,parent,depth+1u);
}
static bool flat_single_child_kind(const char *kind,praktor_orch_type *type,const char **param_key) {
    static const struct {
        const char *name;
        praktor_orch_type type;
        const char *param_key;
    } entries[]={
        {"timeout",PRAKTOR_ORCH_TIMEOUT,"timeout_ms"},
        {"repeat",PRAKTOR_ORCH_REPEAT,"num_cycles"},
        {"delay",PRAKTOR_ORCH_DELAY,"delay_ms"},
        {"keep_running_until_failure",PRAKTOR_ORCH_KEEP_RUNNING_UNTIL_FAILURE,"max_iterations"},
        {"precondition",PRAKTOR_ORCH_PRECONDITION,"condition"},
        {"entry_updated",PRAKTOR_ORCH_ENTRY_UPDATED,"watch_key"},
        {"consume_queue",PRAKTOR_ORCH_CONSUME_QUEUE,"queue_key"},
        {"run_once",PRAKTOR_ORCH_RUN_ONCE,NULL}
    };
    size_t i;
    for(i=0;i<sizeof(entries)/sizeof(entries[0]);++i)
        if(flat_key_matches(kind,entries[i].name)) {
            if(type)*type=entries[i].type;
            if(param_key)*param_key=entries[i].param_key;
            return true;
        }
    return false;
}
static bool flat_key_allowed(const char *key,const char *kind) {
    if(flat_single_child_kind(kind,NULL,NULL))
        return flat_key_matches(key,kind)||flat_key_matches(key,"child");
    if(flat_key_matches(key,"if"))return flat_key_matches(kind,"if");
    if(flat_key_matches(key,"then")||flat_key_matches(key,"else"))return flat_key_matches(kind,"if");
    if(flat_key_matches(key,"while")||flat_key_matches(key,"do")||
       flat_key_matches(key,"max_iterations"))return flat_key_matches(kind,"while");
    if(flat_key_matches(key,"switch")||flat_key_matches(key,"cases"))return flat_key_matches(kind,"switch");
    if(flat_key_matches(key,"set_variable")||flat_key_matches(key,"value")||
       flat_key_matches(key,"from"))return flat_key_matches(kind,"set_variable");
    return false;
}
static int flat_validate_keys(named_build *b,praktor_yaml_node map,const char *kind,
                              bool allow_metadata) {
    size_t i;
    for(i=0;i<praktor_yaml_count(map);++i) {
        tstr key=NULL;
        bool allowed;
        int status=praktor_yaml_key(praktor_yaml_at(map,i),&key,NULL);
        if(status!=SALTS_OK)return fail(b,SALTS_EINVAL,"Cannot decode flat orchestration key");
        allowed=flat_key_allowed(key,kind)||(allow_metadata&&flat_task_metadata_key(key));
        tstr_free(key);
        if(!allowed)return fail(b,SALTS_EINVAL,"Unknown orchestration parameter in flat control form");
    }
    return SALTS_OK;
}
static int flat_validate_integer(named_build *b,tstr text) {
    size_t i=0,n=tstr_len(text),j;
    /* Preserve runtime-substituted scalar admission: any brace bypasses
     * compile-time decimal validation, exactly as the legacy parser does. */
    for(j=0;j<n;++j)if(text[j]=='{'||text[j]=='}')return SALTS_OK;
    if(n&&(text[0]=='+'||text[0]=='-'))i=1u;
    if(i==n) {
        tstr_free(text);
        return fail(b,SALTS_EINVAL,"Orchestration integer parameter must be an integer");
    }
    for(;i<n;++i)if(text[i]<'0'||text[i]>'9') {
        tstr_free(text);
        return fail(b,SALTS_EINVAL,"Orchestration integer parameter must be an integer");
    }
    return SALTS_OK;
}
static int flat_parse(named_build *b,praktor_yaml_node map,size_t parent,unsigned depth,
                      const char *kind,bool allow_metadata) {
    praktor_orch_type type;
    praktor_yaml_node field={0},branch={0};
    const char *single_param=NULL;
    bool single_child=flat_single_child_kind(kind,&type,&single_param);
    size_t index,i;
    int status;
    if(!praktor_yaml_is_map(map))return fail(b,SALTS_EINVAL,"Flat orchestration form must be a mapping");
    status=flat_validate_keys(b,map,kind,allow_metadata);
    if(status!=SALTS_OK)return status;
    if(!single_child) {
        if(flat_key_matches(kind,"if"))type=PRAKTOR_ORCH_IF_THEN_ELSE;
        else if(flat_key_matches(kind,"while"))type=PRAKTOR_ORCH_WHILE_DO;
        else if(flat_key_matches(kind,"switch"))type=PRAKTOR_ORCH_SWITCH;
        else if(flat_key_matches(kind,"set_variable"))type=PRAKTOR_ORCH_SET_VARIABLE;
        else return fail(b,SALTS_EINVAL,"Unsupported flat orchestration form");
    }
    status=flat_append_node(b,type,parent,depth,&index);
    if(status!=SALTS_OK)return status;
    if(single_child) {
        tstr value=NULL;
        if(!flat_find_field(map,kind,&field)||!flat_find_field(map,"child",&branch))
            return fail_at(b,SALTS_EINVAL,map,"Flat decorator requires a scalar and child branch");
        status=praktor_yaml_scalar(field,&value,NULL);
        if(status!=SALTS_OK)
            return fail_at(b,status,field,"Flat decorator value must be a scalar");
        if(single_param) {
            status=flat_add_param(b,index,single_param,value);
            if(status!=SALTS_OK)return status;
        } else tstr_free(value); /* run_once token is deliberately ignored */
        return flat_branch(b,branch,index,depth);
    }
    if(type==PRAKTOR_ORCH_IF_THEN_ELSE) {
        if(!flat_find_field(map,"if",&field))return fail(b,SALTS_EINVAL,"if requires an 'if' condition");
        if(praktor_yaml_has_val(field)) {
            tstr value=NULL;
            status=praktor_yaml_scalar(field,&value,NULL);
            if(status!=SALTS_OK)return fail(b,SALTS_EINVAL,"Orchestration if condition must be scalar, node, or sequence");
            status=flat_add_param(b,index,"condition",value);
            if(status!=SALTS_OK)return status;
        } else {
            status=flat_branch(b,field,index,depth);
            if(status!=SALTS_OK)return status;
        }
        if(!flat_find_field(map,"then",&branch))return fail(b,SALTS_EINVAL,"if requires a then branch");
        status=flat_branch(b,branch,index,depth);
        if(status!=SALTS_OK)return status;
        if(flat_find_field(map,"else",&branch))return flat_branch(b,branch,index,depth);
        return SALTS_OK;
    }
    if(type==PRAKTOR_ORCH_WHILE_DO) {
        if(!flat_find_field(map,"while",&field))return fail(b,SALTS_EINVAL,"while requires a 'while' condition");
        if(praktor_yaml_has_val(field)) {
            tstr value=NULL;
            status=praktor_yaml_scalar(field,&value,NULL);
            if(status!=SALTS_OK)return fail(b,SALTS_EINVAL,"Orchestration while condition must be scalar, node, or sequence");
            status=flat_add_param(b,index,"condition",value);
            if(status!=SALTS_OK)return status;
        } else {
            status=flat_branch(b,field,index,depth);
            if(status!=SALTS_OK)return status;
        }
        if(flat_find_field(map,"max_iterations",&field)) {
            tstr value=NULL;
            status=praktor_yaml_scalar(field,&value,NULL);
            if(status!=SALTS_OK)return fail(b,SALTS_EINVAL,"Orchestration max_iterations must be an integer scalar");
            status=flat_validate_integer(b,value);
            if(status!=SALTS_OK)return status;
            status=flat_add_param(b,index,"max_iterations",value);
            if(status!=SALTS_OK)return status;
        }
        if(!flat_find_field(map,"do",&branch))return fail(b,SALTS_EINVAL,"while requires a do branch");
        return flat_branch(b,branch,index,depth);
    }
    if(type==PRAKTOR_ORCH_SWITCH) {
        tstr value=NULL;
        if(!flat_find_field(map,"switch",&field))return fail(b,SALTS_EINVAL,"switch requires a selector");
        status=praktor_yaml_scalar(field,&value,NULL);
        if(status!=SALTS_OK)return fail(b,SALTS_EINVAL,"Orchestration switch selector must be scalar");
        status=flat_add_param(b,index,"variable",value);
        if(status!=SALTS_OK)return status;
        if(!flat_find_field(map,"cases",&branch))return fail(b,SALTS_EINVAL,"switch requires a cases branch");
        if(!praktor_yaml_is_seq(branch))return fail(b,SALTS_EINVAL,"switch cases must be a sequence");
        for(i=0;i<praktor_yaml_count(branch);++i) {
            status=flat_branch(b,praktor_yaml_at(branch,i),index,depth);
            if(status!=SALTS_OK)return status;
        }
        return SALTS_OK;
    }
    if(!flat_find_field(map,"set_variable",&field))return fail(b,SALTS_EINVAL,"set_variable requires a key");
    {
        tstr value=NULL;
        status=praktor_yaml_scalar(field,&value,NULL);
        if(status!=SALTS_OK)return fail(b,SALTS_EINVAL,"set_variable key must be scalar");
        status=flat_add_param(b,index,"key",value);
        if(status!=SALTS_OK)return status;
    }
    {
        praktor_yaml_node value_node={0},from_node={0};
        bool has_value=flat_find_field(map,"value",&value_node);
        bool has_from=flat_find_field(map,"from",&from_node);
        if(has_value==has_from)return fail(b,SALTS_EINVAL,"set_variable requires exactly one of value or from");
        field=has_value?value_node:from_node;
        {
            tstr value=NULL;
            status=praktor_yaml_scalar(field,&value,NULL);
            if(status!=SALTS_OK)return fail(b,SALTS_EINVAL,"set_variable value/from must be scalar");
            return flat_add_param(b,index,has_value?"value":"from",value);
        }
    }
}
static int flat_publish(named_build *b,praktor_orch_tree **out) {
    praktor_orch_flat_node *input=NULL;
    size_t i;
    int status;
    if(!b->count)return fail(b,SALTS_EINVAL,"Empty orchestration tree");
    input=(praktor_orch_flat_node *)malloc(b->count*sizeof(*input));
    if(!input)return fail(b,SALTS_ENOMEM,"Cannot allocate flat descriptors");
    for(i=0;i<b->count;++i) {
        input[i]=b->rows[i].node;
        input[i].params=input[i].param_count?b->rows[i].params:NULL;
    }
    status=praktor_orch_from_flat_nodes(input,b->count,out,b->error,b->capacity);
    free(input);
    return status;
}

int praktor_orch_from_named_yaml(praktor_yaml_node node,praktor_orch_tree **out,
                                 char *error,size_t error_capacity) {
    named_build b={0};
    praktor_orch_flat_node *input=NULL;
    size_t i;
    int status;
    if(error&&error_capacity)error[0]='\0';
    b.error=error;b.capacity=error_capacity;
    if(!out||(!error&&error_capacity))
        return fail(&b,SALTS_EINVAL,"Invalid YAML orchestration output");
    b.rows=(named_row *)calloc(PRAKTOR_ORCH_MAX_NODES,sizeof(named_row));
    if(!b.rows)return fail(&b,SALTS_ENOMEM,"Cannot allocate orchestration scratch");
    status=parse(&b,node,PRAKTOR_ORCH_ROOT_PARENT,0u);
    if(status!=SALTS_OK)goto done;
    input=(praktor_orch_flat_node *)malloc(b.count*sizeof(*input));
    if(!input) {
        status=fail(&b,SALTS_ENOMEM,"Cannot allocate flat descriptors");
        goto done;
    }
    for(i=0;i<b.count;++i) {
        input[i]=b.rows[i].node;
        input[i].params=input[i].param_count?b.rows[i].params:NULL;
    }
    status=praktor_orch_from_flat_nodes(input,b.count,out,error,error_capacity);
done:
    free(input);
    cleanup(&b);
    return status;
}

int praktor_orch_from_flat_yaml(praktor_yaml_node node,const char *node_type,
                                praktor_orch_tree **out,char *error,size_t error_capacity) {
    named_build b={0};
    int status;
    if(error&&error_capacity)error[0]='\0';
    b.error=error;b.capacity=error_capacity;
    if(!out||(!error&&error_capacity)||!node_type)
        return fail(&b,SALTS_EINVAL,"Invalid flat orchestration input or output");
    b.rows=(named_row *)calloc(PRAKTOR_ORCH_MAX_NODES,sizeof(named_row));
    if(!b.rows)return fail(&b,SALTS_ENOMEM,"Cannot allocate orchestration scratch");
    /* Exactly mirror the temporary C++ parse_orch_params() selector:
     * special flat syntaxes own the full task map; all other catalog names
     * consume just their selected YAML value (ignoring sibling metadata).
     * run_once uses the flat form only with a sibling child. */
    if(strcmp(node_type,"if")==0||strcmp(node_type,"while")==0||
       strcmp(node_type,"switch")==0||strcmp(node_type,"set_variable")==0||
       strcmp(node_type,"timeout")==0||strcmp(node_type,"repeat")==0||
       strcmp(node_type,"delay")==0||
       strcmp(node_type,"keep_running_until_failure")==0||
       strcmp(node_type,"precondition")==0||
       strcmp(node_type,"entry_updated")==0||
       strcmp(node_type,"consume_queue")==0||
       (strcmp(node_type,"run_once")==0&&praktor_yaml_has(node,"child"))) {
        status=flat_parse(&b,node,PRAKTOR_ORCH_ROOT_PARENT,0u,node_type,true);
    } else {
        praktor_yaml_node value={0};
        status=praktor_yaml_get(node,node_type,&value,NULL);
        if(status==SALTS_OK)
            status=parse_named_entry(&b,value,PRAKTOR_ORCH_ROOT_PARENT,0u);
        else status=fail_at(&b,SALTS_EINVAL,node,
                           "Missing named orchestration runner field");
    }
    if(status==SALTS_OK)status=flat_publish(&b,out);
    cleanup(&b);
    return status;
}
