#include "neo.js/compiler/ast_function_argument.h"
#include "neo.js/compiler/asm.h"
#include "neo.js/compiler/ast_expression.h"
#include "neo.js/compiler/ast_identifier.h"
#include "neo.js/compiler/ast_node.h"
#include "neo.js/compiler/ast_pattern_array.h"
#include "neo.js/compiler/ast_pattern_object.h"
#include "neo.js/compiler/ast_pattern_rest.h"
#include "neo.js/compiler/program.h"
#include "neo.js/compiler/scope.h"
#include "neo.js/compiler/token.h"
#include "neo.js/compiler/writer.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/any.h"
#include "neo.js/core/buffer.h"
#include "neo.js/core/location.h"
#include "neo.js/core/position.h"

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
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_NEXT);
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_RESOLVE_NEXT);
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    if (self->value) {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_JNOT_NULL);
      size_t address = neo_buffer_get_size(ctx->program->codes);
      neo_js_program_add_address(allocator, ctx->program, 0);
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      self->value->write(allocator, ctx, self->value);
      neo_js_program_set_current(ctx->program, address);
    }
  }
  if (self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
    char *name = neo_location_get(allocator, self->identifier->location);
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
    neo_js_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  } else {
    self->identifier->write(allocator, ctx, self->identifier);
  }
}
static neo_any_t
neo_serialize_ast_function_argument(neo_allocator_t allocator,
                                    neo_ast_function_argument_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(
      variable, "type",
      neo_create_any_string(allocator, "NEO_NODE_TYPE_FUNCTION_ARGUMENT"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "identifier",
              neo_ast_node_serialize(allocator, node->identifier));
  neo_any_set(variable, "value",
              neo_ast_node_serialize(allocator, node->value));
  return variable;
}

static neo_ast_function_argument_t
neo_create_ast_function_argument(neo_allocator_t allocator) {
  neo_ast_function_argument_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_function_argument_t),
      neo_ast_function_argument_dispose);
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
  neo_ast_node_t error = NULL;
  neo_token_t token = NULL;
  node = neo_create_ast_function_argument(allocator);
  node->identifier = neo_ast_read_identifier(allocator, file, &current);
  if (!node->identifier) {
    node->identifier = neo_ast_read_pattern_array(allocator, file, &current);
  }
  if (!node->identifier) {
    node->identifier = neo_ast_read_pattern_object(allocator, file, &current);
  }
  if (!node->identifier) {
    node->identifier = neo_ast_read_pattern_rest(allocator, file, &current);
  }
  if (!node->identifier) {
    goto onerror;
  }
  NEO_CHECK_NODE(node->identifier, error, onerror);
  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset == '=') {
    current.offset++;
    current.column++;

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    node->value = neo_ast_read_expression_2(allocator, file, &current);
    if (!node->value) {
      goto onerror;
    }
    NEO_CHECK_NODE(node->value, error, onerror);
  }
  error = neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                   node->identifier, NEO_COMPILE_VARIABLE_VAR);
  if (error) {
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
  return error;
}