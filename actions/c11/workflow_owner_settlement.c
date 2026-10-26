#include "workflow_owner_settlement.h"

#include <salts/error_codes.h>
#include <stdint.h>

/* Canonical DAG and TaskRegistry are single-driver. In particular no worker
 * callback may invoke this Owner-only adapter or retain a live Session/Host
 * while it is running. All branch/owner arrays are caller-borrowed. */
static int preflight_cohort(
    const praktor_c11_workflow_owner_cohort *c,
    praktor_c11_workflow_owner_settle_mode mode,size_t max_steps) {
    praktor_workflow_dag_stats dag={0};
    if(!c||!c->dag||!c->registry||!c->task_indices||!c->branch_values||
       !c->branch_succeeded||!c->control||!c->recovery||!c->retainers||
       !c->branch_count||
       c->branch_count>PRAKTOR_C11_PROCESS_COHORT_MAX_RETAINERS||
       !max_steps||c->recovery->marker==0u||c->recovery->sealed||
       mode<PRAKTOR_C11_WORKFLOW_OWNER_PUBLISH||
       mode>PRAKTOR_C11_WORKFLOW_OWNER_ABORT_CANCELLED)
        return PRAKTOR_C11_PARALLEL_INVALID;
    if(praktor_workflow_dag_get_stats(c->dag,&dag)!=SALTS_OK||
       dag.task_count!=praktor_workflow_task_registry_count(c->registry)||
       c->branch_count>dag.task_count)
        return PRAKTOR_C11_PARALLEL_INVALID;
    if(mode==PRAKTOR_C11_WORKFLOW_OWNER_PUBLISH) {
        if(dag.terminal!=PRAKTOR_WORKFLOW_DAG_CONTINUE||!c->task_outputs)
            return PRAKTOR_C11_PARALLEL_INVALID;
    } else if(mode==PRAKTOR_C11_WORKFLOW_OWNER_ABORT_FAILED) {
        if(dag.terminal!=PRAKTOR_WORKFLOW_DAG_CONTINUE&&
           dag.terminal!=PRAKTOR_WORKFLOW_DAG_FAILED)
            return PRAKTOR_C11_PARALLEL_INVALID;
    } else {
        if(dag.terminal!=PRAKTOR_WORKFLOW_DAG_CANCELLED||
           !c->cancelled_before_run)
            return PRAKTOR_C11_PARALLEL_INVALID;
    }
    for(size_t i=0u;i<c->branch_count;++i) {
        const size_t id=c->task_indices[i];
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_FAILED;
        bool reserved=false;
        if(id>=dag.task_count||
           praktor_workflow_dag_is_reserved(c->dag,id,&reserved)!=SALTS_OK||
           !reserved||
           praktor_workflow_task_registry_state(c->registry,id,&state)!=SALTS_OK||
           (state!=PRAKTOR_WORKFLOW_TASK_PENDING&&
            state!=PRAKTOR_WORKFLOW_TASK_RUNNING))
            return PRAKTOR_C11_PARALLEL_INVALID;
        if(mode==PRAKTOR_C11_WORKFLOW_OWNER_PUBLISH) {
            if(!c->branch_values[i].impl||
               c->task_outputs[i].task_index!=id)
                return PRAKTOR_C11_PARALLEL_INVALID;
        }
        for(size_t j=0u;j<i;++j)
            if(c->task_indices[j]==id)return PRAKTOR_C11_PARALLEL_INVALID;
    }
    return PRAKTOR_C11_PARALLEL_OK;
}

int praktor_c11_workflow_owner_close_admission(
    praktor_workflow_dag_state *dag,cflow_executor_control *control,
    cflow_executor_shutdown_policy policy) {
    if(!dag||!control||
       (policy!=CFLOW_EXECUTOR_SHUTDOWN_DRAIN&&
        policy!=CFLOW_EXECUTOR_SHUTDOWN_CANCEL_PENDING))
        return PRAKTOR_C11_PARALLEL_INVALID;
    /* An external cancellation first closes DAG admission on the same Owner
     * turn, before any pending worker's cancel callback can settle. */
    if(policy==CFLOW_EXECUTOR_SHUTDOWN_CANCEL_PENDING&&
       praktor_workflow_dag_stop(dag,PRAKTOR_WORKFLOW_DAG_CANCELLED)!=SALTS_OK)
        return PRAKTOR_C11_PARALLEL_INVARIANT;
    if(!cflow_executor_control_shutdown(control,policy))
        return PRAKTOR_C11_PARALLEL_INVALID;
    return PRAKTOR_C11_PARALLEL_OK;
}

int praktor_c11_workflow_owner_settle_cohort(
    const praktor_c11_workflow_owner_cohort *c,
    praktor_c11_workflow_owner_settle_mode mode,
    size_t max_steps,size_t *out_pending) {
    cflow_executor_protocol_stats stats={0};
    cflow_executor_wait_status waited;
    int result;
    size_t pending=SIZE_MAX;
    if(out_pending)*out_pending=c&&c->recovery&&c->recovery->marker!=0u
        ?c->recovery->last_pending:SIZE_MAX;
    result=preflight_cohort(c,mode,max_steps);
    if(result!=PRAKTOR_C11_PARALLEL_OK)return result;

    if(mode==PRAKTOR_C11_WORKFLOW_OWNER_PUBLISH) {
        /* Original canonical preflight: admission CLOSED, all callbacks
         * finalized+idle, all native leases fully retired, then group commit.
         * No duplicate worker status machine, scheduler, or Process engine. */
        result=praktor_c11_workflow_parallel_commit_after_cflow_and_process_drains(
            c->registry,c->task_outputs,c->branch_values,c->branch_count,
            c->branch_succeeded,c->recovery,c->control,c->retainers,
            c->branch_count,max_steps,out_pending);
        if(result!=PRAKTOR_C11_PARALLEL_OK) {
            if(result==PRAKTOR_C11_PARALLEL_INVARIANT)
                (void)praktor_workflow_dag_stop(
                    c->dag,PRAKTOR_WORKFLOW_DAG_FAILED);
            return result;
        }
        /* Preflight checked every task remained RESERVED and nonterminal
         * before typed/text publication. This Owner turn is uninterrupted;
         * no successor claim can interleave the following status changes.
         * The DAG status changes are not a crash-atomic transaction. */
        for(size_t i=0u;i<c->branch_count;++i) {
            if(praktor_workflow_task_registry_complete(
                   c->registry,c->task_indices[i])!=SALTS_OK||
               praktor_workflow_dag_finish(
                   c->dag,c->task_indices[i],true)!=SALTS_OK) {
                (void)praktor_workflow_dag_stop(
                    c->dag,PRAKTOR_WORKFLOW_DAG_FAILED);
                return PRAKTOR_C11_PARALLEL_INVARIANT;
            }
        }
        return PRAKTOR_C11_PARALLEL_OK;
    }

    /* Failure/cancel recovery is allowed ONLY after all accepted CFlow
     * run/cancel/finalize callbacks have settled. A cancelled-before-run
     * task never owns a Session, and its DAG claim is refused rather than
     * incorrectly marked as an executed failure. */
    if(!cflow_executor_control_get_stats(c->control,&stats))
        return PRAKTOR_C11_PARALLEL_INVALID;
    if(stats.accepted!=c->branch_count||stats.rejected_full||
       stats.rejected_closed||stats.rejected_would_block)
        return PRAKTOR_C11_PARALLEL_INVALID;
    if(stats.lifecycle!=CFLOW_EXECUTOR_CLOSED||
       stats.running!=0u||stats.queued!=0u||
       stats.completed>stats.accepted||
       stats.cancelled>stats.accepted-stats.completed||
       stats.completed+stats.cancelled!=stats.accepted)
        return PRAKTOR_C11_PARALLEL_BUSY;
    waited=cflow_executor_control_wait_idle(c->control);
    if(waited==CFLOW_EXECUTOR_WAIT_PENDING||
       waited==CFLOW_EXECUTOR_WAIT_WOULD_BLOCK)
        return PRAKTOR_C11_PARALLEL_BUSY;
    if(waited!=CFLOW_EXECUTOR_WAIT_IDLE)
        return PRAKTOR_C11_PARALLEL_INVALID;
    if(mode==PRAKTOR_C11_WORKFLOW_OWNER_ABORT_FAILED) {
        if(stats.cancelled!=0u)return PRAKTOR_C11_PARALLEL_CANCELLED;
        if(praktor_workflow_dag_stop(
               c->dag,PRAKTOR_WORKFLOW_DAG_FAILED)!=SALTS_OK)
            return PRAKTOR_C11_PARALLEL_INVARIANT;
    } else {
        size_t cancelled_count=0u;
        for(size_t i=0u;i<c->branch_count;++i)
            if(c->cancelled_before_run[i])++cancelled_count;
        if(cancelled_count!=stats.cancelled)
            return PRAKTOR_C11_PARALLEL_INVALID;
    }
    result=praktor_c11_workflow_parallel_drain_process_retainers(
        c->recovery,c->retainers,c->branch_count,max_steps,&pending,NULL);
    if(out_pending)*out_pending=pending;
    if(result==PRAKTOR_C11_PARALLEL_INVALID)
        return result;
    if(pending==SIZE_MAX)return PRAKTOR_C11_PARALLEL_INVALID;
    if(pending!=0u)
        return result==PRAKTOR_C11_PARALLEL_RECOVERY
             ?PRAKTOR_C11_PARALLEL_RECOVERY:PRAKTOR_C11_PARALLEL_BUSY;

    for(size_t i=0u;i<c->branch_count;++i)
        praktor_c11_value_txn_abort(&c->branch_values[i]);
    /* A failed or cancelled COHORT cannot publish partial candidate outputs.
     * Every executed task is terminal FAILED; no-run queued callbacks retain
     * PENDING TaskRegistry state and release their DAG claims via refuse(). */
    for(size_t i=0u;i<c->branch_count;++i) {
        const size_t id=c->task_indices[i];
        const bool cancelled_no_run=
            mode==PRAKTOR_C11_WORKFLOW_OWNER_ABORT_CANCELLED&&
            c->cancelled_before_run[i];
        if(cancelled_no_run) {
            if(praktor_workflow_dag_refuse(c->dag,id)!=SALTS_OK)
                return PRAKTOR_C11_PARALLEL_INVARIANT;
        } else {
            const vstr reason=vstr_from_cstr(
                mode==PRAKTOR_C11_WORKFLOW_OWNER_ABORT_CANCELLED
                ?"Process cohort cancelled":"Process cohort failed");
            if(praktor_workflow_task_registry_fail(c->registry,id,reason)!=SALTS_OK||
               praktor_workflow_dag_finish(c->dag,id,false)!=SALTS_OK)
                return PRAKTOR_C11_PARALLEL_INVARIANT;
        }
    }
    c->recovery->sealed=true; /* terminal and never publishable on reuse */
    return result==PRAKTOR_C11_PARALLEL_RECOVERY
         ?PRAKTOR_C11_PARALLEL_RECOVERY:PRAKTOR_C11_PARALLEL_OK;
}
