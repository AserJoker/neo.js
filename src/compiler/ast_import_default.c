#include "neo.js/compiler/ast_import_default.h"
#include "neo.js/compiler/asm.h"
#include "neo.js/compiler/ast_identifier.h"
#include "neo.js/compiler/ast_node.h"
#include "neo.js/compiler/program.h"
#include "neo.js/compiler/scope.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/any.h"
#include "neo.js/core/location.h"

static void neo_ast_import_default_dispose(neo_allocator_t allocator,
                                           neo_ast_import_default_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->node.scope);
}

static void neo_ast_import_default_write(neo_allocator_t allocator,
                                         neo_write_context_t ctx,
                                         neo_ast_import_default_t self) {
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
  neo_js_program_add_integer(allocator, ctx->program, 1);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
  neo_js_program_add_string(allocator, ctx->program, "default");
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_GET_FIELD);
  char *name = neo_location_get(allocator, self->identifier->location);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
  neo_js_program_add_string(allocator, ctx->program, name);
  neo_allocator_free(allocator, name);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
}

static neo_any_t
neo_serialize_ast_import_default(neo_allocator_t allocator,
                                 neo_ast_import_default_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_IMPORT_DEFAULT"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "identifier",
              neo_ast_node_serialize(allocator, node->identifier));
  return variable;
}

static neo_ast_import_default_t
neo_create_ast_import_default(neo_allocator_t allocator) {
  neo_ast_import_default_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_import_default_t),
                          neo_ast_import_default_dispose);
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
  neo_ast_node_t error = NULL;
  neo_ast_import_default_t node = neo_create_ast_import_default(allocator);
  node->identifier = neo_ast_read_identifier(allocator, file, &current);
  if (!node->identifier) {
    goto onerror;
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  error =
      neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                               node->identifier, NEO_COMPILE_VARIABLE_CONST);
  if (error) {
    goto onerror;
  };
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return error;
}