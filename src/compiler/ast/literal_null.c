#include "compiler/ast/literal_null.h"
#include "compiler/asm.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/location.h"
#include "core/position.h"

static void neo_ast_literal_null_dispose(neo_allocator_t allocator,
                                         neo_ast_literal_null_t node) {
  neo_allocator_free(allocator, node->node.scope);
}

static neo_any_t neo_serialize_ast_literal_null(neo_allocator_t allocator,
                                                neo_ast_literal_null_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_LITERAL_NU"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  return variable;
}
static void neo_ast_literal_null_write(neo_allocator_t allocator,
                                       neo_write_context_t ctx,
                                       neo_ast_literal_null_t self) {
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_NULL);
}
static neo_ast_literal_null_t
neo_create_ast_literal_null(neo_allocator_t allocator) {
  neo_ast_literal_null_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_literal_null_t),
                          neo_ast_literal_null_dispose);
  node->node.type = NEO_NODE_TYPE_LITERAL_NULL;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_literal_null;
  node->node.resolve_closure = neo_ast_node_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_literal_null_write;
  return node;
}

neo_ast_node_t neo_ast_read_literal_null(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_literal_null_t node = NULL;
  neo_ast_node_t error = NULL;
  neo_token_t token = neo_read_identify_token(allocator, file, &current);
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (!token || !neo_location_is(token->location, "null")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  node = neo_create_ast_literal_null(allocator);
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}