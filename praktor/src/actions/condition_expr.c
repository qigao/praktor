#include "actions/session_internal.h"
#include "actions/variable_reference.h"
#include "expressions/expression_lexer.h"
#include <query_vm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  GUARD_SOURCE_BYTES = 4096,
  GUARD_MAX_INSTRUCTIONS = 256,
  GUARD_MAX_OPERANDS = 256,
  GUARD_MAX_DEPTH = 32
};
static const qvm_limits_t guard_limits = {.max_instructions = GUARD_MAX_INSTRUCTIONS,
                                          .max_operands = GUARD_MAX_OPERANDS,
                                          .max_regexes = 1,
                                          .max_steps = GUARD_MAX_INSTRUCTIONS};
typedef enum guard_comparison {
  GUARD_EQ,
  GUARD_NE,
  GUARD_LT,
  GUARD_LE,
  GUARD_GT,
  GUARD_GE
} guard_comparison;
typedef enum guard_value_kind {
  GUARD_INVALID,
  GUARD_BOOLEAN,
  GUARD_INTEGER,
  GUARD_NUMBER,
  GUARD_STRING
} guard_value_kind;
typedef enum guard_precedence {
  PREC_OR = 1,
  PREC_AND,
  PREC_COMPARE,
  PREC_BIT_OR,
  PREC_BIT_XOR,
  PREC_BIT_AND,
  PREC_SHIFT,
  PREC_SUM,
  PREC_PRODUCT,
  PREC_PREFIX
} guard_precedence;
typedef struct guard_operator {
  const char *text;
  guard_precedence precedence;
  qvm_opcode_t opcode;
  uint32_t argument;
} guard_operator;
/* Bitwise and arithmetic operators bind inside integer comparisons. */
static const guard_operator guard_operators[] = {{"||", PREC_OR, QVM_OP_JMP_TRUE, 0u},
                                                 {"&&", PREC_AND, QVM_OP_JMP_FALSE, 0u},
                                                 {"==", PREC_COMPARE, QVM_OP_CMP, GUARD_EQ},
                                                 {"!=", PREC_COMPARE, QVM_OP_CMP, GUARD_NE},
                                                 {"<", PREC_COMPARE, QVM_OP_CMP, GUARD_LT},
                                                 {"<=", PREC_COMPARE, QVM_OP_CMP, GUARD_LE},
                                                 {">", PREC_COMPARE, QVM_OP_CMP, GUARD_GT},
                                                 {">=", PREC_COMPARE, QVM_OP_CMP, GUARD_GE},
                                                 {"|", PREC_BIT_OR, QVM_OP_BOR, 0u},
                                                 {"^", PREC_BIT_XOR, QVM_OP_BXOR, 0u},
                                                 {"&", PREC_BIT_AND, QVM_OP_BAND, 0u},
                                                 {"<<", PREC_SHIFT, QVM_OP_LSHIFT, 0u},
                                                 {">>", PREC_SHIFT, QVM_OP_RSHIFT, 0u},
                                                 {"+", PREC_SUM, QVM_OP_ADD, 0u},
                                                 {"-", PREC_SUM, QVM_OP_SUB, 0u},
                                                 {"*", PREC_PRODUCT, QVM_OP_MUL, 0u},
                                                 {"/", PREC_PRODUCT, QVM_OP_DIV, 0u},
                                                 {"%", PREC_PRODUCT, QVM_OP_MOD, 0u}};
typedef struct guard_operand {
  vstr field; /* Empty for a literal. Otherwise borrows the node's plan text. */
  int64_t integer;
} guard_operand;
struct praktor_condition_expr {
  qvm_instruction_t *instructions;
  guard_operand *operands;
  uint32_t instruction_count;
};
typedef struct guard_parser {
  praktor_expression_lexer lexer;
  praktor_expression_token token;
  char text[GUARD_SOURCE_BYTES + 1];
  const vstr *fields;
  size_t field_count;
  qvm_instruction_t instructions[GUARD_MAX_INSTRUCTIONS];
  guard_operand operands[GUARD_MAX_OPERANDS];
  uint32_t instruction_count, operand_count;
  char *error;
  size_t error_capacity;
  int status;
} guard_parser;

static bool guard_error(guard_parser *parser, int status, const char *message) {
  parser->status = status;
  if (parser->error && parser->error_capacity)
    (void)snprintf(parser->error, parser->error_capacity, "guard expr at byte %zu: %s",
                   parser->token.position + 1u, message);
  return false;
}

static bool next(guard_parser *parser) {
  size_t required;
  int status = praktor_expression_lexer_next(&parser->lexer, &parser->token, parser->text,
                                             sizeof parser->text, &required);
  if (status == SALTS_OK)
    return true;
  parser->token.position = parser->lexer.error_position;
  return guard_error(parser, status, "Invalid token");
}

static bool token_is(const guard_parser *parser, praktor_expression_token_kind kind,
                     const char *text) {
  return parser->token.kind == kind &&
         (!text || vstr_eq(vstr_from_buf(parser->text, parser->token.size), vstr_from_cstr(text)));
}

static bool emit(guard_parser *parser, qvm_instruction_t instruction) {
  if (parser->instruction_count == GUARD_MAX_INSTRUCTIONS)
    return guard_error(parser, SALTS_ENOBUFS, "Instruction limit exceeded");
  parser->instructions[parser->instruction_count++] = instruction;
  return true;
}

static bool decimal(vstr digits, bool negative, int64_t *out) {
  const uint64_t limit = (uint64_t)INT64_MAX + (negative ? 1u : 0u);
  uint64_t value = 0u;
  if (!digits.len)
    return false;
  for (size_t i = 0u; i < digits.len; ++i) {
    if (digits.data[i] < '0' || digits.data[i] > '9')
      return false;
    const unsigned digit = (unsigned)(digits.data[i] - '0');
    if (value > (limit - digit) / 10u)
      return false;
    value = value * 10u + digit;
  }
  *out = negative ? (value == limit ? INT64_MIN : -(int64_t)value) : (int64_t)value;
  return true;
}

static bool field_operand(guard_parser *parser, guard_operand *out) {
  const vstr name = vstr_from_buf(parser->text, parser->token.size);
  for (size_t i = 0u; i < parser->field_count; ++i) {
    if (vstr_eq(name, parser->fields[i])) {
      out->field = parser->fields[i];
      return next(parser);
    }
  }
  return guard_error(parser, SALTS_EINVAL, "Variable is not declared in fields");
}

static bool emit_operand(guard_parser *parser, guard_operand operand, uint16_t dst,
                         qvm_opcode_t opcode) {
  if (parser->operand_count == GUARD_MAX_OPERANDS)
    return guard_error(parser, SALTS_ENOBUFS, "Operand limit exceeded");
  const uint32_t index = parser->operand_count++;
  parser->operands[index] = operand;
  return emit(parser, (qvm_instruction_t){.op = opcode, .dst = dst, .src1 = index});
}

static bool integer_literal(guard_parser *parser, uint16_t dst, bool negative) {
  guard_operand operand = {0};
  if (!decimal(vstr_from_buf(parser->text, parser->token.size), negative, &operand.integer))
    return guard_error(parser, SALTS_EINVAL, "Expected a signed 64-bit decimal integer");
  return next(parser) && emit_operand(parser, operand, dst, QVM_OP_LOAD_CONST);
}

static bool parse_presence(guard_parser *parser, uint16_t dst) {
  guard_operand field = {0};
  if (!next(parser))
    return false;
  if (!token_is(parser, PRAKTOR_EXPR_LPAREN, NULL))
    return guard_error(parser, SALTS_EINVAL, "Expected '(' after has");
  if (!next(parser))
    return false;
  if (!token_is(parser, PRAKTOR_EXPR_VARIABLE, NULL))
    return guard_error(parser, SALTS_EINVAL, "Expected a variable in has()");
  if (!field_operand(parser, &field))
    return false;
  if (!token_is(parser, PRAKTOR_EXPR_RPAREN, NULL))
    return guard_error(parser, SALTS_EINVAL, "Expected ')' after has argument");
  return next(parser) && emit_operand(parser, field, dst, QVM_OP_EXISTS);
}

static bool parse_expr(guard_parser *parser, unsigned precedence, unsigned depth, uint16_t dst,
                       guard_value_kind *kind);

static bool parse_prefix(guard_parser *parser, unsigned depth, uint16_t dst,
                         guard_value_kind *kind) {
  if (token_is(parser, PRAKTOR_EXPR_OPERATOR, "!")) {
    /* Existing guards bind ! to an entire comparison, e.g. !$count > 0. */
    if (!next(parser) || !parse_expr(parser, PREC_COMPARE, depth + 1u, dst, kind))
      return false;
    if (*kind != GUARD_BOOLEAN)
      return guard_error(parser, SALTS_EINVAL, "Logical negation requires a predicate");
    return emit(parser, (qvm_instruction_t){.op = QVM_OP_NOT, .dst = dst, .src1 = dst});
  }
  if (token_is(parser, PRAKTOR_EXPR_LPAREN, NULL)) {
    if (!next(parser) || !parse_expr(parser, PREC_OR, depth + 1u, dst, kind))
      return false;
    if (!token_is(parser, PRAKTOR_EXPR_RPAREN, NULL))
      return guard_error(parser, SALTS_EINVAL, "Expected ')'");
    return next(parser);
  }
  const bool truth = token_is(parser, PRAKTOR_EXPR_IDENTIFIER, "true");
  if (truth || token_is(parser, PRAKTOR_EXPR_IDENTIFIER, "false")) {
    *kind = GUARD_BOOLEAN;
    return next(parser) &&
           emit(parser, (qvm_instruction_t){.op = truth ? QVM_OP_TRUE : QVM_OP_FALSE, .dst = dst});
  }
  if (token_is(parser, PRAKTOR_EXPR_IDENTIFIER, "has")) {
    *kind = GUARD_BOOLEAN;
    return parse_presence(parser, dst);
  }
  *kind = GUARD_INTEGER;
  if (token_is(parser, PRAKTOR_EXPR_VARIABLE, NULL)) {
    guard_operand operand = {0};
    return field_operand(parser, &operand) && emit_operand(parser, operand, dst, QVM_OP_LOAD_PATH);
  }
  if (token_is(parser, PRAKTOR_EXPR_NUMBER, NULL))
    return integer_literal(parser, dst, false);
  const bool negative = token_is(parser, PRAKTOR_EXPR_OPERATOR, "-");
  const bool positive = token_is(parser, PRAKTOR_EXPR_OPERATOR, "+");
  const bool complement = token_is(parser, PRAKTOR_EXPR_OPERATOR, "~");
  if (!negative && !positive && !complement)
    return guard_error(parser, SALTS_EINVAL, "Expected an integer or predicate");
  if (!next(parser))
    return false;
  if (!complement && token_is(parser, PRAKTOR_EXPR_NUMBER, NULL))
    return integer_literal(parser, dst, negative);
  if (!parse_expr(parser, PREC_PREFIX, depth + 1u, dst, kind))
    return false;
  if (*kind != GUARD_INTEGER)
    return guard_error(parser, SALTS_EINVAL, "Numeric unary operator requires an integer");
  return positive ||
         emit(parser, (qvm_instruction_t){
                          .op = negative ? QVM_OP_NEG : QVM_OP_BNOT, .dst = dst, .src1 = dst});
}

static const guard_operator *current_operator(const guard_parser *parser) {
  if (!token_is(parser, PRAKTOR_EXPR_OPERATOR, NULL))
    return NULL;
  for (size_t i = 0u; i < sizeof guard_operators / sizeof guard_operators[0]; ++i)
    if (strcmp(parser->text, guard_operators[i].text) == 0)
      return &guard_operators[i];
  return NULL;
}

static bool parse_expr(guard_parser *parser, unsigned precedence, unsigned depth, uint16_t dst,
                       guard_value_kind *kind) {
  if (depth > GUARD_MAX_DEPTH)
    return guard_error(parser, SALTS_ENOBUFS, "Expression nesting limit exceeded");
  if (dst >= QVM_MAX_REGISTERS)
    return guard_error(parser, SALTS_ENOBUFS, "Register limit exceeded");
  if (!parse_prefix(parser, depth, dst, kind))
    return false;
  const guard_operator *op;
  while ((op = current_operator(parser)) && op->precedence >= precedence) {
    const bool logical = op->precedence <= PREC_AND;
    const guard_value_kind expected = logical ? GUARD_BOOLEAN : GUARD_INTEGER;
    const char *message =
        logical ? "Logical operator requires predicates" : "Numeric operator requires integers";
    if (*kind != expected)
      return guard_error(parser, SALTS_EINVAL, message);
    const uint32_t jump = parser->instruction_count;
    /* A short-circuited operator retains its left result in dst. */
    if (logical && !emit(parser, (qvm_instruction_t){.op = op->opcode, .src1 = dst}))
      return false;
    const uint16_t right_register = logical ? dst : (uint16_t)(dst + 1u);
    guard_value_kind right_kind;
    if (!next(parser) ||
        !parse_expr(parser, op->precedence + 1u, depth, right_register, &right_kind))
      return false;
    if (right_kind != expected)
      return guard_error(parser, SALTS_EINVAL, message);
    if (logical)
      parser->instructions[jump].arg = parser->instruction_count;
    else if (!emit(parser, (qvm_instruction_t){.op = op->opcode,
                                               .dst = dst,
                                               .arg = op->argument,
                                               .src1 = dst,
                                               .src2 = right_register}))
      return false;
    *kind = (logical || op->opcode == QVM_OP_CMP) ? GUARD_BOOLEAN : GUARD_INTEGER;
  }
  return true;
}

static bool field_name_valid(vstr name) {
  return name.len && praktor_variable_name_size(name, false) == name.len &&
         !praktor_retired_variable_namespace(name);
}

int praktor_condition_expr_compile(vstr source, const vstr *fields, size_t count,
                                   praktor_condition_expr **out, char *error, size_t capacity) {
  guard_parser parser = {
      .fields = fields, .field_count = count, .error = error, .error_capacity = capacity};
  if (source.len > GUARD_SOURCE_BYTES) {
    guard_error(&parser, SALTS_ENOBUFS, "Source length limit exceeded");
    return parser.status;
  }
  for (size_t i = 0u; i < count; ++i) {
    if (!field_name_valid(fields[i])) {
      guard_error(&parser, SALTS_EINVAL, "Invalid field name");
      return parser.status;
    }
    for (size_t earlier = 0u; earlier < i; ++earlier) {
      if (vstr_eq(fields[i], fields[earlier])) {
        guard_error(&parser, SALTS_EINVAL, "Duplicate field name");
        return parser.status;
      }
    }
  }
  (void)praktor_expression_lexer_init(&parser.lexer, source.data, source.len);
  parser.lexer.integer_operators = 1;
  guard_value_kind kind;
  if (!next(&parser) || !parse_expr(&parser, PREC_OR, 0u, 0u, &kind))
    return parser.status;
  if (!token_is(&parser, PRAKTOR_EXPR_END, NULL)) {
    guard_error(&parser, SALTS_EINVAL, "Unexpected trailing token");
    return parser.status;
  }
  if (kind != GUARD_BOOLEAN) {
    guard_error(&parser, SALTS_EINVAL, "Guard expression must produce a predicate");
    return parser.status;
  }
  qvm_diagnostic_t detail = {0};
  const int verified = qvm_verify_slice_ex(parser.instructions, parser.instruction_count, 0u,
                                           parser.instruction_count, QVM_MAX_REGISTERS,
                                           parser.operand_count, 0u, &guard_limits, &detail);
  if (verified != QVM_STATUS_OK) {
    if (error && capacity)
      (void)snprintf(error, capacity, "guard expr bytecode: %s", detail.message);
    return verified == QVM_STATUS_NO_MEMORY ? SALTS_ENOMEM : SALTS_EINVAL;
  }
  praktor_condition_expr *program = calloc(1u, sizeof(*program));
  if (!program)
    return SALTS_ENOMEM;
  const size_t instruction_bytes = parser.instruction_count * sizeof(*program->instructions);
  program->instructions = malloc(instruction_bytes);
  if (!program->instructions) {
    praktor_condition_expr_destroy(program);
    return SALTS_ENOMEM;
  }
  if (parser.operand_count) {
    const size_t operand_bytes = parser.operand_count * sizeof(*program->operands);
    program->operands = malloc(operand_bytes);
    if (!program->operands) {
      praktor_condition_expr_destroy(program);
      return SALTS_ENOMEM;
    }
    memcpy(program->operands, parser.operands, operand_bytes);
  }
  memcpy(program->instructions, parser.instructions, instruction_bytes);
  program->instruction_count = parser.instruction_count;
  *out = program;
  return SALTS_OK;
}

void praktor_condition_expr_destroy(praktor_condition_expr *program) {
  if (!program)
    return;
  free(program->operands);
  free(program->instructions);
  free(program);
}

typedef struct guard_context {
  session_impl *session;
  const praktor_condition_expr *program;
  int status;
} guard_context;

static void field_key(vstr field, char *key) {
  memcpy(key, field.data, field.len);
  key[field.len] = '\0';
}

static int resolve(void *user, uint32_t index, qvm_value_t *out) {
  guard_context *context = user;
  const guard_operand *operand = &context->program->operands[index];
  int64_t value = operand->integer;
  if (context->status != SALTS_OK)
    return 0;
  if (operand->field.len) {
    char key[SALTS_SELECTOR_MAX_STRING_BYTES_V1 + 1u];
    session_text text;
    field_key(operand->field, key);
    context->status = session_get(context->session, key, 0u, &text);
    if (context->status != SALTS_OK)
      return 0;
    const bool negative = text.size && text.data[0] == '-';
    const size_t sign = negative || (text.size && text.data[0] == '+') ? 1u : 0u;
    if (!decimal(vstr_from_buf(text.data + sign, text.size - sign), negative, &value)) {
      context->status =
          session_error(context->session, SALTS_EINVAL,
                        "guard expr field '%s' requires a signed 64-bit decimal integer", key);
      return 0;
    }
  }
  *out = (qvm_value_t){.type = GUARD_INTEGER, .integer = value};
  return 1;
}

static int exists(void *user, uint32_t index, int *out) {
  guard_context *context = user;
  char key[SALTS_SELECTOR_MAX_STRING_BYTES_V1 + 1u];
  bool found;
  field_key(context->program->operands[index].field, key);
  context->status = session_has(context->session, key, &found);
  if (context->status != SALTS_OK)
    return 0;
  *out = found;
  return 1;
}

static int truthy(void *user, const qvm_value_t *value) {
  (void)user;
  return value->type == GUARD_BOOLEAN && value->boolean;
}

static void make_invalid(void *user, qvm_value_t *out) {
  (void)user;
  *out = (qvm_value_t){.type = GUARD_INVALID};
}
static void make_bool(void *user, int boolean, qvm_value_t *out) {
  (void)user;
  *out = (qvm_value_t){.type = GUARD_BOOLEAN, .boolean = boolean};
}
static void make_number(void *user, double number, qvm_value_t *out) {
  (void)user;
  *out = (qvm_value_t){.type = GUARD_NUMBER, .number = number};
}
static void make_string(void *user, const char *bytes, size_t length, qvm_value_t *out) {
  (void)user;
  *out = (qvm_value_t){.type = GUARD_STRING, .str = bytes, .length = length};
}

static int compare(int64_t a, int64_t b, uint32_t operation, qvm_value_t *out) {
  bool matched;
  switch (operation) {
  case GUARD_EQ:
    matched = a == b;
    break;
  case GUARD_NE:
    matched = a != b;
    break;
  case GUARD_LT:
    matched = a < b;
    break;
  case GUARD_LE:
    matched = a <= b;
    break;
  case GUARD_GT:
    matched = a > b;
    break;
  case GUARD_GE:
    matched = a >= b;
    break;
  default:
    return 0;
  }
  make_bool(NULL, matched, out);
  return 1;
}

/* Interpret a 64-bit bit pattern as a signed value without an out-of-range
 * unsigned-to-signed conversion. */
static int64_t signed_bits(uint64_t bits) {
  if (bits <= (uint64_t)INT64_MAX)
    return (int64_t)bits;
  return -1 - (int64_t)(UINT64_MAX - bits);
}

static bool product_overflows(int64_t a, int64_t b) {
  if (a > 0)
    return b > 0 ? a > INT64_MAX / b : b < INT64_MIN / a;
  if (a < 0)
    return b > 0 ? a < INT64_MIN / b : (b < 0 && a < INT64_MAX / b);
  return false;
}

static int operation_error(guard_context *context, qvm_opcode_t opcode, int status,
                           const char *reason) {
  context->status =
      session_error(context->session, status, "guard expr %s: %s", qvm_opcode_name(opcode), reason);
  return 0;
}

static int binary(void *user, qvm_opcode_t opcode, uint32_t argument, const qvm_value_t *left,
                  const qvm_value_t *right, qvm_value_t *out) {
  guard_context *context = user;
  if (left->type != GUARD_INTEGER || right->type != GUARD_INTEGER)
    return 0;
  const int64_t a = left->integer, b = right->integer;
  if (opcode == QVM_OP_CMP)
    return compare(a, b, argument, out);
  int64_t result;
  switch (opcode) {
  case QVM_OP_ADD:
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
      goto overflow;
    result = a + b;
    break;
  case QVM_OP_SUB:
    if ((b > 0 && a < INT64_MIN + b) || (b < 0 && a > INT64_MAX + b))
      goto overflow;
    result = a - b;
    break;
  case QVM_OP_MUL:
    if (product_overflows(a, b))
      goto overflow;
    result = a * b;
    break;
  case QVM_OP_DIV:
  case QVM_OP_MOD:
    if (b == 0)
      return operation_error(context, opcode, SALTS_EINVAL, "division by zero");
    if (a == INT64_MIN && b == -1)
      goto overflow;
    result = opcode == QVM_OP_DIV ? a / b : a % b;
    break;
  case QVM_OP_BAND:
    result = signed_bits((uint64_t)a & (uint64_t)b);
    break;
  case QVM_OP_BOR:
    result = signed_bits((uint64_t)a | (uint64_t)b);
    break;
  case QVM_OP_BXOR:
    result = signed_bits((uint64_t)a ^ (uint64_t)b);
    break;
  case QVM_OP_LSHIFT:
  case QVM_OP_RSHIFT:
    if (b < 0 || b >= 64)
      return operation_error(context, opcode, SALTS_ERANGE, "shift count must be between 0 and 63");
    result = signed_bits(opcode == QVM_OP_LSHIFT ? (uint64_t)a << (unsigned)b
                                                 : (uint64_t)a >> (unsigned)b);
    break;
  default:
    return 0;
  }
  *out = (qvm_value_t){.type = GUARD_INTEGER, .integer = result};
  return 1;
overflow:
  return operation_error(context, opcode, SALTS_ERANGE, "signed 64-bit integer overflow");
}

static int unary(void *user, qvm_opcode_t opcode, const qvm_value_t *input, qvm_value_t *out) {
  if (input->type != GUARD_INTEGER)
    return 0;
  const int64_t value = input->integer;
  if (opcode == QVM_OP_BNOT) {
    *out = (qvm_value_t){.type = GUARD_INTEGER, .integer = signed_bits(~(uint64_t)value)};
    return 1;
  }
  if (opcode != QVM_OP_NEG)
    return 0;
  if (value == INT64_MIN)
    return operation_error(user, opcode, SALTS_ERANGE, "signed 64-bit integer overflow");
  *out = (qvm_value_t){.type = GUARD_INTEGER, .integer = -value};
  return 1;
}

int session_condition_expr(session_impl *session, const praktor_condition_expr *program,
                           salts_bt_node_status *status) {
  static const qvm_exec_ops_t ops = {.resolve = resolve,
                                     .truthy = truthy,
                                     .binary = binary,
                                     .unary = unary,
                                     .exists = exists,
                                     .make_invalid = make_invalid,
                                     .make_bool = make_bool,
                                     .make_number = make_number,
                                     .make_string = make_string};
  guard_context context = {session, program, SALTS_OK};
  qvm_value_t result;
  qvm_diagnostic_t detail = {0};
  const int evaluated = qvm_execute_ex(program->instructions, program->instruction_count, 0u,
                                       program->instruction_count, &ops, &context, NULL, &result,
                                       &guard_limits, &detail);
  if (context.status != SALTS_OK)
    return context.status;
  if (evaluated != QVM_STATUS_OK)
    return session_error(session, SALTS_EINVAL, "guard expr instruction %u: %s", detail.instruction,
                         detail.message);
  if (result.type != GUARD_BOOLEAN)
    return session_error(session, SALTS_EINVAL, "guard expr bytecode did not return a predicate");
  *status = result.boolean ? SALTS_BT_SUCCESS : SALTS_BT_FAILURE;
  return SALTS_OK;
}
