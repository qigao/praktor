#include "yml/workflow_dag_state.h"
#include <stdlib.h>

typedef enum dag_task_phase {
    DAG_TASK_PENDING = 0,
    DAG_TASK_RESERVED,
    DAG_TASK_FINISHED,
    DAG_TASK_REFUSED
} dag_task_phase;

typedef struct dag_task_entry {
    size_t remaining;
    dag_task_phase phase;
} dag_task_entry;

struct praktor_workflow_dag_state {
    const praktor_workflow_dependencies *graph; /* borrowed immutable graph */
    dag_task_entry *tasks;
    size_t count, max_active, active, completed;
    praktor_workflow_dag_terminal terminal;
};

int praktor_workflow_dag_state_create(
    const praktor_workflow_dependencies *graph, size_t max_active,
    praktor_workflow_dag_state **out) {
    praktor_workflow_dag_state *state;
    size_t count, edges, i;
    if(!graph||!out||!max_active)return SALTS_EINVAL;
    count=praktor_workflow_dependencies_task_count(graph);
    edges=praktor_workflow_dependencies_edge_count(graph);
    if(!count||count>PRAKTOR_WORKFLOW_MAX_TASKS||
       edges>PRAKTOR_WORKFLOW_MAX_EDGES)return SALTS_EINVAL;
    state=(praktor_workflow_dag_state *)calloc(1u,sizeof(*state));
    if(!state)return SALTS_ENOMEM;
    state->tasks=(dag_task_entry *)calloc(count,sizeof(*state->tasks));
    if(!state->tasks){free(state);return SALTS_ENOMEM;}
    state->graph=graph;
    state->count=count;
    state->max_active=max_active;
    for(i=0u;i<edges;++i) {
        size_t from=0u,to=0u;
        if(praktor_workflow_dependencies_edge_at(graph,i,&from,&to)!=SALTS_OK||
           from>=count||to>=count) {
            praktor_workflow_dag_state_destroy(state);
            return SALTS_EINVAL;
        }
        ++state->tasks[to].remaining; /* edge_count <= 16384 */
    }
    *out=state;
    return SALTS_OK;
}

void praktor_workflow_dag_state_destroy(praktor_workflow_dag_state *state) {
    if(!state)return;
    free(state->tasks);
    free(state);
}

int praktor_workflow_dag_claim_ready(praktor_workflow_dag_state *state,
    size_t *out_indices,size_t capacity,size_t *out_count) {
    size_t admitted=0u,i,limit;
    if(!state||!out_indices||!capacity||!out_count)return SALTS_EINVAL;
    *out_count=0u;
    if(state->terminal!=PRAKTOR_WORKFLOW_DAG_CONTINUE)return SALTS_EBUSY;
    if(state->active>=state->max_active)return SALTS_OK;
    limit=state->max_active-state->active;
    if(limit>capacity)limit=capacity;
    for(i=0u;i<state->count&&admitted<limit;++i) {
        dag_task_entry *row=&state->tasks[i];
        if(row->phase!=DAG_TASK_PENDING||row->remaining)continue;
        row->phase=DAG_TASK_RESERVED;
        out_indices[admitted++]=i;
    }
    state->active+=admitted;
    *out_count=admitted;
    return SALTS_OK;
}

int praktor_workflow_dag_finish(praktor_workflow_dag_state *state,
    size_t task_index,bool success) {
    size_t i,edges;
    if(!state||task_index>=state->count||
       state->tasks[task_index].phase!=DAG_TASK_RESERVED)
        return SALTS_EINVAL;
    /* All edge_at() lookups are guaranteed to succeed for a live immutable
     * graph because create() checked them. A graph lifetime violation is an
     * invalid caller contract; state is single-owner and may not recover. */
    state->tasks[task_index].phase=DAG_TASK_FINISHED;
    --state->active;
    ++state->completed;
    if(!success&&state->terminal==PRAKTOR_WORKFLOW_DAG_CONTINUE)
        state->terminal=PRAKTOR_WORKFLOW_DAG_FAILED;
    if(state->terminal!=PRAKTOR_WORKFLOW_DAG_CONTINUE)return SALTS_OK;
    edges=praktor_workflow_dependencies_edge_count(state->graph);
    for(i=0u;i<edges;++i) {
        size_t from,to;
        if(praktor_workflow_dependencies_edge_at(state->graph,i,&from,&to)!=SALTS_OK||
           from>=state->count||to>=state->count)
            return SALTS_EINVAL;
        if(from==task_index) {
            if(!state->tasks[to].remaining)return SALTS_EINVAL;
            --state->tasks[to].remaining;
        }
    }
    return SALTS_OK;
}

int praktor_workflow_dag_refuse(praktor_workflow_dag_state *state,
    size_t task_index) {
    if(!state||task_index>=state->count||
       state->tasks[task_index].phase!=DAG_TASK_RESERVED)
        return SALTS_EINVAL;
    state->tasks[task_index].phase=DAG_TASK_REFUSED;
    --state->active;
    if(state->terminal==PRAKTOR_WORKFLOW_DAG_CONTINUE)
        state->terminal=PRAKTOR_WORKFLOW_DAG_ADMISSION_REFUSED;
    return SALTS_OK;
}

int praktor_workflow_dag_stop(praktor_workflow_dag_state *state,
    praktor_workflow_dag_terminal reason) {
    if(!state||reason<PRAKTOR_WORKFLOW_DAG_FAILED||
       reason>PRAKTOR_WORKFLOW_DAG_ADMISSION_REFUSED)return SALTS_EINVAL;
    if(state->terminal==PRAKTOR_WORKFLOW_DAG_CONTINUE)
        state->terminal=reason;
    return SALTS_OK;
}

int praktor_workflow_dag_get_stats(const praktor_workflow_dag_state *state,
    praktor_workflow_dag_stats *out) {
    size_t i,ready=0u;
    if(!state||!out)return SALTS_EINVAL;
    if(state->terminal==PRAKTOR_WORKFLOW_DAG_CONTINUE)
        for(i=0u;i<state->count;++i)
            if(state->tasks[i].phase==DAG_TASK_PENDING&&
               !state->tasks[i].remaining)++ready;
    out->task_count=state->count;
    out->active=state->active;
    out->completed=state->completed;
    out->ready=ready;
    out->terminal=state->terminal;
    out->quiescent=state->active==0u&&
        (state->terminal!=PRAKTOR_WORKFLOW_DAG_CONTINUE||
         state->completed==state->count);
    out->all_succeeded=state->terminal==PRAKTOR_WORKFLOW_DAG_CONTINUE&&
                       state->completed==state->count&&state->active==0u;
    return SALTS_OK;
}
