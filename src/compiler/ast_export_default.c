#include "neo.js/compiler/ast_export_default.h"
#include "neo.js/compiler/asm.h"
#include "neo.js/compiler/ast_expression.h"
#include "neo.js/compiler/ast_node.h"
#include "neo.js/compiler/program.h"
#include "neo.js/compiler/token.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/any.h"
#include "neo.js/core/location.h"
#include "neo.js/core/position.h"
#include <stdbool.h>
#include <stdio.h>

static void neo_ast_export_default_dispose(neo_allocator_t allocator,
                                           neo_ast_export_default_t node) {
  neo_allocator_free(allocator, node->value);
  neo_allocator_free(allocator, node->node.scope);
}

static void neo_ast_export_default_write(neo_allocator_t allocator,
                                         neo_write_context_t ctx,
                                         neo_ast_export_default_t self) {
  neo_ast_node_t error = NULL;
  self->value->write(allocator, ctx, self->value);
  neo_js_program_add_string(allocator, ctx->program, "default");
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_EXPORT);
}

static void
neo_ast_export_default_resolve_closure(neo_allocator_t allocator,
                                       neo_ast_export_default_t node,
                                       neo_list_t closure) {
  node->value->resolve_closure(allocator, node->value, closure);
}

static neo_any_t
neo_serialize_ast_export_default(neo_allocator_t allocator,
                                 neo_ast_export_default_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_EXPORT_DEFAULT"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "value",
              neo_ast_node_serialize(allocator, node->value));
  return variable;
}

static neo_ast_export_default_t
neo_create_ast_export_default(neo_allocator_t allocator) {
  neo_ast_export_default_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_export_default_t),
                          neo_ast_export_default_dispose);
  node->node.type = NEO_NODE_TYPE_EXPORT_DEFAULT;
  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_export_default;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_export_default_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_export_default_write;
  node->value = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_export_default(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position) {
  neo_ast_node_t error = NULL;
  neo_position_t current = *position;
  neo_ast_export_default_t node = neo_create_ast_export_default(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (!token || !neo_location_is(token->location, "default")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  node->value = neo_ast_read_expression_2(allocator, file, &current);
  if (!node->value) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  NEO_CHECK_NODE(node->value, error, onerror);
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