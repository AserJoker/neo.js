#include "neo.js/compiler/ast_statement_throw.h"
#include "neo.js/compiler/asm.h"
#include "neo.js/compiler/ast_expression.h"
#include "neo.js/compiler/ast_node.h"
#include "neo.js/compiler/token.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/any.h"
#include "neo.js/core/location.h"
#include "neo.js/core/position.h"
#include <stdio.h>

static void neo_ast_statement_throw_dispose(neo_allocator_t allocator,
                                            neo_ast_statement_throw_t node) {
  neo_allocator_free(allocator, node->value);
  neo_allocator_free(allocator, node->node.scope);
}
static void
neo_ast_statement_throw_resolve_closure(neo_allocator_t allocator,
                                        neo_ast_statement_throw_t self,
                                        neo_list_t closure) {
  self->value->resolve_closure(allocator, self->value, closure);
}
static void neo_ast_statement_throw_write(neo_allocator_t allocator,
                                          neo_write_context_t ctx,
                                          neo_ast_statement_throw_t self) {
  if (self->value) {
    self->value->write(allocator, ctx, self->value);
  } else {
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_UNDEFINED);
  }
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_THROW);
}
static neo_any_t
neo_serialize_ast_statement_throw(neo_allocator_t allocator,
                                  neo_ast_statement_throw_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(
      variable, "type",
      neo_create_any_string(allocator, "NEO_NODE_TYPE_STATEMENT_THROW"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "value",
              neo_ast_node_serialize(allocator, node->value));
  return variable;
}
static neo_ast_statement_throw_t
neo_ast_create_statement_throw(neo_allocator_t allocator) {
  neo_ast_statement_throw_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_statement_throw_t),
                          neo_ast_statement_throw_dispose);
  node->node.type = NEO_NODE_TYPE_STATEMENT_THROW;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_statement_throw;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_statement_throw_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_statement_throw_write;
  node->value = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_statement_throw(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_node_t error = NULL;
  neo_ast_statement_throw_t node = neo_ast_create_statement_throw(allocator);
  token = neo_read_identify_token(allocator, file, &current);
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (!token || !neo_location_is(token->location, "throw")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  neo_position_t cur = current;
  uint32_t line = current.line;
  error = neo_skip_all(allocator, file, &cur);
  if (error) {
    goto onerror;
  }
  if (cur.line == line) {
    node->value = neo_ast_read_expression(allocator, file, &cur);
    if (node->value) {
      current = cur;
      uint32_t line = current.line;

      error = neo_skip_all(allocator, file, &current);
      if (error) {
        goto onerror;
      }
      if (current.line == line) {
        if (*current.offset && *current.offset != ';' &&
            *current.offset != '}') {
          error = neo_create_error_node(
              allocator,
              "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
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
  return error;
}