#include "compiler/ast/pattern_rest.h"
#include "compiler/asm.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/node.h"
#include "compiler/ast/pattern_array.h"
#include "compiler/ast/pattern_object.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
static void neo_ast_pattern_rest_dispose(neo_allocator_t allocator,
                                         neo_ast_pattern_rest_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->node.scope);
}
static void neo_ast_pattern_rest_resolve_closure(neo_allocator_t allocator,
                                                 neo_ast_pattern_rest_t self,
                                                 neo_list_t closure) {
  self->identifier->resolve_closure(allocator, self->identifier, closure);
}
static void neo_ast_pattern_rest_write(neo_allocator_t allocator,
                                       neo_write_context_t ctx,
                                       neo_ast_pattern_rest_t self) {
  neo_program_add_code(allocator, ctx->program, NEO_ASM_REST);
  if (self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
    wchar_t *name = neo_location_get(allocator, self->identifier->location);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  } else {
    TRY(self->identifier->write(allocator, ctx, self->identifier)) { return; }
  }
}
static neo_variable_t
neo_serialize_ast_pattern_rest(neo_allocator_t allocator,
                               neo_ast_pattern_rest_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, L"type",
      neo_create_variable_string(allocator, L"NEO_NODE_TYPE_PATTERN_REST"));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, L"identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  return variable;
}

static neo_ast_pattern_rest_t
neo_create_ast_pattern_rest(neo_allocator_t allocator) {

  neo_ast_pattern_rest_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_pattern_rest_t),
                          neo_ast_pattern_rest_dispose);
  node->identifier = NULL;
  node->node.type = NEO_NODE_TYPE_PATTERN_REST;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_pattern_rest;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_pattern_rest_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_pattern_rest_write;
  return node;
}

neo_ast_node_t neo_ast_read_pattern_rest(neo_allocator_t allocator,
                                         const wchar_t *file,
                                         neo_position_t *position) {
  neo_ast_pattern_rest_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  token = neo_read_symbol_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "...")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node = neo_create_ast_pattern_rest(allocator);
  node->identifier = TRY(neo_ast_read_identifier(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_pattern_array(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_pattern_object(allocator, file, &current)) {
      goto onerror;
    }
  }
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