#include "compiler/ast/expression_super.h"
#include "compiler/asm.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"

static void neo_ast_expression_super_dispose(neo_allocator_t allocator,
                                             neo_ast_expression_super_t node) {
  neo_allocator_free(allocator, node->node.scope);
}

static neo_variable_t
neo_serialize_ast_expression_super(neo_allocator_t allocator,
                                   neo_ast_expression_super_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_EXPRESSION_SUPER"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static void neo_ast_expression_super_write(neo_allocator_t allocator,
                                           neo_write_context_t ctx,
                                           neo_ast_expression_super_t self) {
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_SUPER);
}

static neo_ast_expression_super_t
neo_create_ast_expression_super(neo_allocator_t allocator) {
  neo_ast_expression_super_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_super);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_SUPER;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_expression_super;
  node->node.resolve_closure = neo_ast_node_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_expression_super_write;
  return node;
}

neo_ast_node_t neo_ast_read_expression_super(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_super_t node = NULL;
  neo_token_t token = TRY(neo_read_identify_token(allocator, file, &current)) {
    goto onerror;
  }
  if (!token || !neo_location_is(token->location, "super")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  node = neo_create_ast_expression_super(allocator);
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}