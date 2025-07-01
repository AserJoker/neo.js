#include "compiler/ast/statement_return.h"
#include "compiler/asm.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_statement_return_dispose(neo_allocator_t allocator,
                                             neo_ast_statement_return_t node) {
  neo_allocator_free(allocator, node->value);
  neo_allocator_free(allocator, node->node.scope);
}
static void
neo_ast_statement_return_resolve_closure(neo_allocator_t allocator,
                                         neo_ast_statement_return_t self,
                                         neo_list_t closure) {
  if (self->value) {
    self->value->resolve_closure(allocator, self->value, closure);
  }
}
static void neo_ast_statement_return_write(neo_allocator_t allocator,
                                           neo_write_context_t ctx,
                                           neo_ast_statement_return_t self) {
  if (self->value) {
    TRY(self->value->write(allocator, ctx, self->value)) { return; }
  } else {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_UNDEFINED);
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_RET);
}
static neo_variable_t
neo_serialize_ast_statement_return(neo_allocator_t allocator,
                                   neo_ast_statement_return_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_STATEMENT_RETURN"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "value",
                   neo_ast_node_serialize(allocator, node->value));
  return variable;
}
static neo_ast_statement_return_t
neo_ast_create_statement_return(neo_allocator_t allocator) {
  neo_ast_statement_return_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_return);
  node->node.type = NEO_NODE_TYPE_STATEMENT_RETURN;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_statement_return;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_statement_return_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_statement_return_write;
  node->value = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_statement_return(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_statement_return_t node = neo_ast_create_statement_return(allocator);
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "return")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  neo_position_t cur = current;
  uint32_t line = current.line;
  SKIP_ALL(allocator, file, &cur, onerror);
  if (cur.line == line) {
    node->value = TRY(neo_ast_read_expression(allocator, file, &cur)) {
      goto onerror;
    }
    if (node->value) {
      current = cur;
      uint32_t line = current.line;
      SKIP_ALL(allocator, file, &current, onerror);
      if (current.line == line) {
        if (*current.offset && *current.offset != ';' &&
            *current.offset != '}') {
          THROW("Invalid or unexpected token \n  at %s:%d:%d", file,
                current.line, current.column);
          goto onerror;
        }
      }
    }
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  neo_allocator_free(allocator, token);
  return NULL;
}