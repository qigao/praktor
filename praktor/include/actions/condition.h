#ifndef PRAKTOR_ACTIONS_CONDITION_H
#define PRAKTOR_ACTIONS_CONDITION_H

#include "actions/session.h"
#include <core/vstr.h>
#include <selector.h>

typedef struct praktor_condition_expr praktor_condition_expr;
typedef struct praktor_condition {
    selector_program_t *match;
    praktor_condition_expr *expr;
} praktor_condition;

/* out starts empty. Condition programs are immutable and may borrow field
 * names from the node's parameter text until destroy. Legacy nodes leave out
 * empty. Diagnostics may be omitted with zero capacity. */
int praktor_condition_compile(const praktor_session_node *node,
                              praktor_condition *out,char *error,size_t capacity);
void praktor_condition_destroy(praktor_condition *condition);

int praktor_condition_expr_compile(vstr source,const vstr *fields,size_t count,
                                   praktor_condition_expr **out,
                                   char *error,size_t capacity);
void praktor_condition_expr_destroy(praktor_condition_expr *expr);

#endif
