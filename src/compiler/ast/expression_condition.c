#include "compiler/ast/expression_condition.h"
#include "compiler/asm.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void
neo_ast_expression_condition_dispose(neo_allocator_t allocator,
                                     neo_ast_expression_condition_t node) {
  neo_allocator_free(allocator, node->condition);
  neo_allocator_free(allocator, node->alternate);
  neo_allocator_free(allocator, node->consequent);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_expression_condition_write(neo_allocator_t allocator,
                                   neo_write_context_t ctx,
                                   neo_ast_expression_condition_t self) {
  TRY(self->condition->write(allocator, ctx, self->condition)) { return; }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_JFALSE);
  size_t address = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  TRY(self->consequent->write(allocator, ctx, self->consequent)) { return; }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_JMP);
  size_t end = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  neo_program_set_current(ctx->program, address);
  TRY(self->alternate->write(allocator, ctx, self->alternate)) { return; }
  neo_program_set_current(ctx->program, end);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
}

static void neo_ast_expression_condition_resolve_closure(
    neo_allocator_t allocator, neo_ast_expression_condition_t self,
    neo_list_t closure) {
  self->condition->resolve_closure(allocator, self->condition, closure);
  self->alternate->resolve_closure(allocator, self->alternate, closure);
  self->consequent->resolve_closure(allocator, self->consequent, closure);
}

static neo_variable_t
neo_serialize_ast_expression_condition(neo_allocator_t allocator,
                                       neo_ast_expression_condition_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_EXPRESSION_CONDITION"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "condition",
                   neo_ast_node_serialize(allocator, node->condition));
  neo_variable_set(variable, "alternate",
                   neo_ast_node_serialize(allocator, node->alternate));
  neo_variable_set(variable, "consequent",
                   neo_ast_node_serialize(allocator, node->consequent));
  return variable;
}

static neo_ast_expression_condition_t
neo_create_ast_expression_condition(neo_allocator_t allocator) {
  neo_ast_expression_condition_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_condition);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_CONDITION;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_expression_condition;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_expression_condition_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_expression_condition_write;
  node->condition = NULL;
  node->alternate = NULL;
  node->consequent = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_expression_condition(neo_allocator_t allocator,
                                                 const wchar_t *file,
                                                 neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_expression_condition_t node =
      neo_create_ast_expression_condition(allocator);
  node->condition = TRY(neo_ast_read_expression_3(allocator, file, &current)) {
    goto onerror;
  };
  SKIP_ALL(allocator, file, &current, onerror);
  token = TRY(neo_read_symbol_token(allocator, file, &current)) {
    goto onerror;
  };
  if (!token || !neo_location_is(token->location, "?")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  node->consequent = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
    goto onerror;
  };
  if (!node->consequent) {
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  token = TRY(neo_read_symbol_token(allocator, file, &current)) {
    goto onerror;
  };
  if (!token || !neo_location_is(token->location, ":")) {
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->alternate = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
    goto onerror;
  };
  if (!node->alternate) {
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
          current.line, current.column);
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