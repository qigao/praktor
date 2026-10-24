#ifndef PRAKTOR_EXPRESSION_REGEX_SALTS_H
#define PRAKTOR_EXPRESSION_REGEX_SALTS_H

#include "expressions/expression_evaluator.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Installs the bounded Salts/Core byte-regex callbacks while preserving the
 * Host's variable resolver and user pointer.  Compiled handles are owned by
 * the evaluator and searches retain exact byte lengths, including NUL. */
int praktor_expression_regex_salts_bind(praktor_expression_host *host);

#ifdef __cplusplus
}
#endif

#endif
