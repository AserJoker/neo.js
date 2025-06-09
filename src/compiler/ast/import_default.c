#include "compiler/ast/import_default.h"
#include "compiler/asm.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "core/allocator.h"
#include "core/location.h"
#include "core/variable.h"

static void neo_ast_import_default_dispose(neo_allocator_t allocator,
                                           neo_ast_import_default_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->node.scope);
}

static void neo_ast_import_default_write(neo_allocator_t allocator,
                                         neo_write_context_t ctx,
                                         neo_ast_import_default_t self) {
  neo_program_add_code(ctx->program, NEO_ASM_PUSH_VALUE);
  neo_program_add_integer(ctx->program, 1);
  neo_program_add_code(ctx->program, NEO_ASM_PUSH_STRING);
  neo_program_add_string(ctx->program, "default");
  neo_program_add_code(ctx->program, NEO_ASM_GET_FIELD);
  char *name = neo_location_get(allocator, self->identifier->location);
  neo_program_add_code(ctx->program, NEO_ASM_STORE);
  neo_program_add_string(ctx->program, name);
  neo_allocator_free(allocator, name);
}

static neo_variable_t
neo_serialize_ast_import_default(neo_allocator_t allocator,
                                 neo_ast_import_default_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_IMPORT_DEFAULT"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  return variable;
}

static neo_ast_import_default_t
neo_create_ast_import_default(neo_allocator_t allocator) {
  neo_ast_import_default_t node =
      neo_allocator_alloc2(allocator, neo_ast_import_default);
  node->node.type = NEO_NODE_TYPE_IMPORT_DEFAULT;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_import_default;
  node->node.resolve_closure = neo_ast_node_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_import_default_write;
  node->identifier = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_import_default(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_import_default_t node = neo_create_ast_import_default(allocator);
  node->identifier = neo_ast_read_identifier(allocator, file, &current);
  if (!node->identifier) {
    goto onerror;
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  TRY(neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                               node->identifier, NEO_COMPILE_VARIABLE_CONST)) {
    goto onerror;
  };
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}