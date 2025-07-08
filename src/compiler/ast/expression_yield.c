#include "compiler/ast/expression_yield.h"
#include "compiler/asm.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdbool.h>

static void neo_ast_expression_yield_dispose(neo_allocator_t allocator,
                                             neo_ast_expression_yield_t node) {
  neo_allocator_free(allocator, node->value);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_expression_yield_resolve_closure(neo_allocator_t allocator,
                                         neo_ast_expression_yield_t self,
                                         neo_list_t closure) {
  if (self->value) {
    self->value->resolve_closure(allocator, self->value, closure);
  }
}

static void neo_ast_expression_yield_write(neo_allocator_t allocator,
                                           neo_write_context_t ctx,
                                           neo_ast_expression_yield_t self) {
  if (self->value) {
    TRY(self->value->write(allocator, ctx, self->value)) { return; }
  } else {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_UNDEFINED);
  }
  if (self->degelate) {
    if (!ctx->is_async) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_ITERATOR);
    } else {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_ASYNC_ITERATOR);
    }
    size_t begin = neo_buffer_get_size(ctx->program->codes);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_NEXT);
    if (ctx->is_async) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_AWAIT);
    }
    neo_program_add_code(allocator, ctx->program, NEO_ASM_JTRUE);
    size_t addr = neo_buffer_get_size(ctx->program->codes);
    neo_program_add_address(allocator, ctx->program, 0);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_YIELD);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_JMP);
    neo_program_add_address(allocator, ctx->program, begin);
    neo_program_set_current(ctx->program, addr);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  } else {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_YIELD);
  }
}

static neo_variable_t
neo_serialize_ast_expression_yield(neo_allocator_t allocator,
                                   neo_ast_expression_yield_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_EXPRESSION_YIELD"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "value",
                   neo_ast_node_serialize(allocator, node->value));
  neo_variable_set(variable, "degelate",
                   neo_create_variable_boolean(allocator, node->degelate));
  return variable;
}

static neo_ast_expression_yield_t
neo_create_ast_expression_yield(neo_allocator_t allocator) {
  neo_ast_expression_yield_t node =
      (neo_ast_expression_yield_t)neo_allocator_alloc2(
          allocator, neo_ast_expression_yield);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_YIELD;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_expression_yield;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_expression_yield_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_expression_yield_write;
  node->value = NULL;
  node->degelate = false;
  return node;
}

neo_ast_node_t neo_ast_read_expression_yield(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {

  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_expression_yield_t node = NULL;
  token = TRY(neo_read_identify_token(allocator, file, &current)) {
    goto onerror;
  };
  if (!token) {
    return NULL;
  }
  if (!neo_location_is(token->location, "yield")) {
    goto onerror;
  }
  if (!neo_compile_scope_is_generator()) {
    THROW("yield only used in generator context\n  at _.compile(%s:%d:%d)",
          file, position->line, position->column);
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node = neo_create_ast_expression_yield(allocator);
  if (*current.offset == '*') {
    current.offset++;
    current.column++;
    node->degelate = true;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  node->value = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
    goto onerror;
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