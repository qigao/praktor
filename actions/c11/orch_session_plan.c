#include "orch_session_plan.h"
#include "plan.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct orch_visit {
    const praktor_orch_node *node;
    size_t parent, depth;
} orch_visit;

struct praktor_c11_orch_session_plan {
    praktor_c11_session_node *nodes;
    size_t count, capacity, retained_bytes;
};

static int fail(char *error,size_t cap,int code,const char *reason) {
    if(error&&cap)(void)snprintf(error,cap,"%s",reason);
    return code;
}
/* The Legacy AST bridge performs Mustache expansion before invoking native
 * actions. Until a native equivalent is qualified, double-brace templates
 * must fail closed rather than be interpreted as single-brace value lookups. */
static bool contains_mustache(const char *text,size_t length) {
    size_t i;
    for(i=1u;i<length;++i)
        if((text[i-1u]=='{'&&text[i]=='{')||
           (text[i-1u]=='}'&&text[i]=='}'))return true;
    return false;
}
static bool supported(praktor_orch_type type) {
    switch(type) {
    case PRAKTOR_ORCH_SEQUENCE:
    case PRAKTOR_ORCH_FALLBACK:
    case PRAKTOR_ORCH_SELECTOR:
    case PRAKTOR_ORCH_INVERTER:
    case PRAKTOR_ORCH_FORCE_SUCCESS:
    case PRAKTOR_ORCH_FORCE_FAILURE:
    case PRAKTOR_ORCH_SET_VARIABLE:
    case PRAKTOR_ORCH_CHECK_EXIT_CODE:
    case PRAKTOR_ORCH_PARSE_KEY_VALUE:
    case PRAKTOR_ORCH_PARSE_REGEX:
    case PRAKTOR_ORCH_PARSE_LINES:
    case PRAKTOR_ORCH_SLEEP:
        return true;
    default: return false;
    }
}
void praktor_c11_orch_session_plan_destroy(
    praktor_c11_orch_session_plan *plan) {
    size_t i,j;
    if(!plan)return;
    for(i=0u;i<plan->count;++i) {
        praktor_c11_session_node *node=&plan->nodes[i];
        /* Only values are allocated: canonical names and types are immutable
         * catalog literals and remain valid for the entire process. */
        for(j=0u;j<node->parameter_count;++j)
            free((void *)node->parameters[j].text);
        free((void *)node->parameters);
        free((void *)node->text_sizes);
    }
    free(plan->nodes);
    free(plan);
}
size_t praktor_c11_orch_session_plan_node_count(
    const praktor_c11_orch_session_plan *plan) {
    return plan?plan->count:0u;
}

int praktor_c11_orch_session_plan_create(
    const praktor_orch_tree *tree,const salts_bt_tree_config *limits,
    praktor_c11_orch_session_plan **out,char *error,size_t cap) {
    praktor_c11_orch_session_plan *plan=NULL;
    orch_visit *stack=NULL;
    size_t node_capacity,top=0u;
    int status=SALTS_OK;
    if(error&&cap)error[0]='\0';
    if(!tree||!limits||!out||(!error&&cap))
        return fail(error,cap,SALTS_EINVAL,"Invalid C11 Orch Session Plan input");
    node_capacity=praktor_orch_tree_size(tree);
    if(!node_capacity||node_capacity>PRAKTOR_ORCH_MAX_NODES||
       !limits->max_nodes||node_capacity>limits->max_nodes||
       !limits->max_depth||limits->max_depth>SALTS_BT_MAX_DEPTH||
       !limits->max_control_iterations||
       limits->max_control_iterations>SALTS_BT_MAX_CONTROL_ITERATIONS)
        return fail(error,cap,SALTS_EINVAL,"Orch Session Plan exceeds C11 behavior-tree limits");
    plan=(praktor_c11_orch_session_plan *)calloc(1u,sizeof(*plan));
    if(!plan)return fail(error,cap,SALTS_ENOMEM,"Cannot allocate Orch Session Plan");
    plan->capacity=node_capacity;
    plan->nodes=(praktor_c11_session_node *)calloc(
        node_capacity,sizeof(*plan->nodes));
    stack=(orch_visit *)calloc(node_capacity,sizeof(*stack));
    if(!plan->nodes||!stack) {
        status=fail(error,cap,SALTS_ENOMEM,"Cannot allocate bounded Orch Session Plan nodes");
        goto done;
    }
    stack[top++]=(orch_visit){
        praktor_orch_tree_root(tree),PRAKTOR_C11_BT_NO_PARENT,1u
    };
    while(top) {
        const orch_visit visit=stack[--top];
        const praktor_orch_spec *spec;
        const size_t node_index=plan->count;
        size_t children,params,j;
        praktor_c11_leaf_parameter *copies=NULL;
        size_t *sizes=NULL;
        praktor_c11_session_node compiled={0};
        bool prune=false;
        praktor_orch_type type;
        if(!visit.node||node_index>=node_capacity||
           visit.depth>limits->max_depth) {
            status=fail(error,cap,SALTS_EINVAL,"Invalid Orch tree or excessive depth");
            goto done;
        }
        type=praktor_orch_node_type(visit.node);
        spec=praktor_orch_spec_for(type);
        if(!spec) {
            status=fail(error,cap,SALTS_EINVAL,"Invalid Orch action type");
            goto done;
        }
        if(!supported(type)) {
            status=fail(error,cap,SALTS_ENOTSUP,
                "Orch action not qualified for native C11 Session; no Legacy fallback");
            goto done;
        }
        children=praktor_orch_child_count(visit.node);
        params=praktor_orch_param_count(visit.node);
        if(params>PRAKTOR_ORCH_MAX_PARAMS||children>=node_capacity||
           children>node_capacity-top-1u) {
            status=fail(error,cap,SALTS_EINVAL,"Invalid Orch parameter/child budget");
            goto done;
        }
        /* Reserve the partial slot first; failed parameter allocation must
         * still be freed by the unique plan destructor. */
        ++plan->count;
        plan->nodes[node_index].parameter_count=params;
        if(params) {
            copies=(praktor_c11_leaf_parameter *)calloc(params,sizeof(*copies));
            sizes=(size_t *)calloc(params,sizeof(*sizes));
            plan->nodes[node_index].parameters=copies;
            plan->nodes[node_index].text_sizes=sizes;
            if(!copies||!sizes) {
                status=fail(error,cap,SALTS_ENOMEM,"Cannot allocate C11 typed ports");
                goto done;
            }
        }
        for(j=0u;j<params;++j) {
            praktor_orch_param view={0};
            char *value;
            if(praktor_orch_param_at(visit.node,j,&view)!=SALTS_OK||
               !vstr_is_valid(view.key)||!vstr_is_valid(view.value)||
               !view.key.data||strlen(view.key.data)!=view.key.len) {
                status=fail(error,cap,SALTS_EINVAL,"Invalid canonical Orch parameter");
                goto done;
            }
            /* Native Host leaf value ports are NUL-terminated strings.
             * Reject embedded NUL instead of silently truncating text. */
            if(view.value.len && memchr(view.value.data,'\0',view.value.len)) {
                status=fail(error,cap,SALTS_ENOTSUP,
                    "Binary Orch parameter cannot enter native C11 leaf");
                goto done;
            }
            if(view.value.len && contains_mustache(view.value.data,view.value.len)) {
                status=fail(error,cap,SALTS_ENOTSUP,
                    "Orch Mustache expansion not qualified for native C11 Session");
                goto done;
            }
            if(plan->retained_bytes>=PRAKTOR_ORCH_MAX_BYTES||
               view.value.len>=PRAKTOR_ORCH_MAX_BYTES-plan->retained_bytes) {
                status=fail(error,cap,SALTS_EINVAL,"Orch Session Plan text budget exceeded");
                goto done;
            }
            value=(char *)malloc(view.value.len+1u);
            if(!value) {
                status=fail(error,cap,SALTS_ENOMEM,"Cannot copy Orch parameter text");
                goto done;
            }
            if(view.value.len)memcpy(value,view.value.data,view.value.len);
            value[view.value.len]='\0';
            copies[j].name=view.key.data; /* immutable catalog literal */
            copies[j].kind=PRAKTOR_C11_LEAF_STRING;
            copies[j].text=value;
            sizes[j]=view.value.len;
            plan->retained_bytes+=view.value.len+1u;
        }
        status=praktor_c11_compile_node(
            limits,spec->name,copies,params,sizes,children,visit.parent,
            &compiled,&prune,error,cap);
        if(status!=SALTS_OK)goto done;
        /* The admitted initial subset has no static child pruning. Refuse
         * unexpected compiler pruning rather than silently changing the AST. */
        if(prune) {
            status=fail(error,cap,SALTS_ENOTSUP,
                "Native C11 Orch static pruning not yet qualified");
            goto done;
        }
        plan->nodes[node_index]=compiled;
        for(j=children;j>0u;--j) {
            const praktor_orch_node *child=NULL;
            if(praktor_orch_child_at(visit.node,j-1u,&child)!=SALTS_OK||
               !child||top>=node_capacity) {
                status=fail(error,cap,SALTS_EINVAL,"Broken Orch preorder child reference");
                goto done;
            }
            stack[top++]=(orch_visit){child,node_index,visit.depth+1u};
        }
    }
    if(plan->count!=node_capacity) {
        status=fail(error,cap,SALTS_EINVAL,
            "Orch Session Plan contains unreachable or repeated nodes");
        goto done;
    }
    *out=plan;
    plan=NULL;
done:
    free(stack);
    praktor_c11_orch_session_plan_destroy(plan);
    return status;
}
int praktor_c11_orch_session_plan_init_session(
    const praktor_c11_orch_session_plan *plan,
    const praktor_c11_session_config *config,
    praktor_c11_session *session) {
    if(!plan||!plan->count||!config||!session)return SALTS_EINVAL;
    return praktor_c11_session_init(session,config,plan->nodes,plan->count);
}
