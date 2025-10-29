#include "compiler/ast/statement_expression.h"
#include "compiler/asm.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"

static void
neo_ast_statement_expression_dispose(neo_allocator_t allocator,
                                     neo_ast_statement_expression_t node) {
  neo_allocator_free(allocator, node->expression);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_statement_expression_write(neo_allocator_t allocator,
                                   neo_write_context_t ctx,
                                   neo_ast_statement_expression_t self) {
  TRY(self->expression->write(allocator, ctx, self->expression)) { return; }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_SAVE);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
}

static void neo_ast_statement_expression_resolve_closure(
    neo_allocator_t allocator, neo_ast_statement_expression_t self,
    neo_list_t closure) {
  self->expression->resolve_closure(allocator, self->expression, closure);
}

static neo_any_t
neo_serialize_ast_statement_expreesion(neo_allocator_t allocator,
                                       neo_ast_statement_expression_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(
      variable, "type",
      neo_create_any_string(allocator, "NEO_NODE_TYPE_STATEMENT_EXPRESSION"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "expression",
              neo_ast_node_serialize(allocator, node->expression));
  return variable;
}
static neo_ast_statement_expression_t
neo_create_ast_statement_expreesion(neo_allocator_t allocator) {
  neo_ast_statement_expression_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_statement_expression_t),
      neo_ast_statement_expression_dispose);
  node->node.type = NEO_NODE_TYPE_STATEMENT_EXPRESSION;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_statement_expreesion;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_statement_expression_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_statement_expression_write;
  node->expression = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_statement_expression(neo_allocator_t allocator,
                                                 const char *file,
                                                 neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_statement_expression_t node = NULL;
  neo_ast_node_t expression =
      TRY(neo_ast_read_expression(allocator, file, &current)) {
    goto onerror;
  }
  if (!expression) {
    return NULL;
  }
  node = neo_create_ast_statement_expreesion(allocator);
  node->expression = expression;
  uint32_t line = current.line;
  SKIP_ALL(allocator, file, &current, onerror);
  if (current.line == line) {
    if (*current.offset && *current.offset != ';' && *current.offset != '}') {
      THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
      goto onerror;
    }
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}