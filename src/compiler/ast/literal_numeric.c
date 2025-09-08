#include "compiler/ast/literal_numeric.h"
#include "compiler/asm.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdlib.h>

static void neo_ast_literal_numeric_dispose(neo_allocator_t allocator,
                                            neo_ast_literal_numeric_t node) {
  neo_allocator_free(allocator, node->node.scope);
}

static neo_variable_t
neo_serialize_ast_literal_numeric(neo_allocator_t allocator,
                                  neo_ast_literal_numeric_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_LITERAL_NU"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static void neo_ast_literal_numeric_write(neo_allocator_t allocator,
                                          neo_write_context_t ctx,
                                          neo_ast_literal_numeric_t self) {
  if (self->node.type == NEO_NODE_TYPE_LITERAL_BIGINT) {
    char *name = neo_location_get(allocator, self->node.location);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_BIGINT);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  } else if (neo_location_is(self->node.location, "NaN")) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_NAN);
  } else if (neo_location_is(self->node.location, "Infinity")) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_INFINTY);
  } else {
    char *str = neo_location_get(allocator, self->node.location);
    double value = atof(str);
    neo_allocator_free(allocator, str);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_NUMBER);
    neo_program_add_number(allocator, ctx->program, value);
  }
}

static neo_ast_literal_numeric_t
neo_create_ast_literal_numeric(neo_allocator_t allocator) {
  neo_ast_literal_numeric_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_literal_numeric_t),
                          neo_ast_literal_numeric_dispose);
  node->node.type = NEO_NODE_TYPE_LITERAL_NUMERIC;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_literal_numeric;
  node->node.resolve_closure = neo_ast_node_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_literal_numeric_write;
  return node;
}

neo_ast_node_t neo_ast_read_literal_numeric(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_literal_numeric_t node = NULL;
  neo_token_t token = TRY(neo_read_number_token(allocator, file, &current)) {
    goto onerror;
  }
  if (!token) {
    token = TRY(neo_read_identify_token(allocator, file, &current)) {
      goto onerror;
    }
    if (token && !neo_location_is(token->location, "NaN") &&
        !neo_location_is(token->location, "Infinity")) {
      goto onerror;
    }
  }
  if (!token) {
    return NULL;
  }
  neo_allocator_free(allocator, token);
  node = neo_create_ast_literal_numeric(allocator);
  neo_position_t cur = current;
  token = TRY(neo_read_identify_token(allocator, file, &cur)) { goto onerror; }
  if (token && neo_location_is(token->location, "n")) {
    node->node.type = NEO_NODE_TYPE_LITERAL_BIGINT;
    current = cur;
  }
  neo_allocator_free(allocator, token);
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