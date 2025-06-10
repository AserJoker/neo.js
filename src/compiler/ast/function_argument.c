#include "compiler/ast/function_argument.h"
#include "compiler/asm.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/node.h"
#include "compiler/ast/pattern_array.h"
#include "compiler/ast/pattern_object.h"
#include "compiler/ast/pattern_rest.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"

static void
neo_ast_function_argument_dispose(neo_allocator_t allocator,
                                  neo_ast_function_argument_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->value);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_function_argument_resolve_closure(neo_allocator_t allocator,
                                          neo_ast_function_argument_t self,
                                          neo_list_t closure) {
  if (self->value) {
    self->value->resolve_closure(allocator, self->value, closure);
  }
  self->identifier->resolve_closure(allocator, self->identifier, closure);
}
static void neo_ast_function_argument_write(neo_allocator_t allocator,
                                            neo_write_context_t ctx,
                                            neo_ast_function_argument_t self) {
  if (self->identifier->type != NEO_NODE_TYPE_PATTERN_REST) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_NEXT);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    if (self->value) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_JNOT_NULL);
      size_t address = neo_buffer_get_size(ctx->program->codes);
      neo_program_add_address(allocator, ctx->program, 0);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      TRY(self->value->write(allocator, ctx, self->value)) { return; }
      neo_program_set_current(ctx->program, address);
    }
  }
  if (self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
    char *name = neo_location_get(allocator, self->identifier->location);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  } else {
    TRY(self->identifier->write(allocator, ctx, self->identifier)) { return; }
  }
}
static neo_variable_t
neo_serialize_ast_function_argument(neo_allocator_t allocator,
                                    neo_ast_function_argument_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_FUNCTION_ARGUMENT"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  neo_variable_set(variable, "value",
                   neo_ast_node_serialize(allocator, node->value));
  return variable;
}

static neo_ast_function_argument_t
neo_create_ast_function_argument(neo_allocator_t allocator) {
  neo_ast_function_argument_t node =
      neo_allocator_alloc2(allocator, neo_ast_function_argument);
  node->identifier = NULL;
  node->value = NULL;
  node->node.type = NEO_NODE_TYPE_FUNCTION_ARGUMENT;

  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_function_argument;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_function_argument_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_function_argument_write;
  return node;
}

neo_ast_node_t neo_ast_read_function_argument(neo_allocator_t allocator,
                                              const char *file,
                                              neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_function_argument_t node = NULL;
  neo_token_t token = NULL;
  node = neo_create_ast_function_argument(allocator);
  node->identifier = TRY(neo_ast_read_identifier(allocator, file, &current)) {
    goto onerror;
  };
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
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_pattern_rest(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset == '=') {
    current.offset++;
    current.column++;
    SKIP_ALL(allocator, file, &current, onerror);
    node->value = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
      goto onerror;
    }
    if (!node->value) {
      goto onerror;
    }
  }
  TRY(neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                               node->identifier, NEO_COMPILE_VARIABLE_VAR)) {
    goto onerror;
  };
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