#include "compiler/ast/statement_if.h"
#include "compiler/asm.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/node.h"
#include "compiler/ast/statement.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/buffer.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include <stdio.h>

static void neo_ast_statement_if_dispose(neo_allocator_t allocator,
                                         neo_ast_statement_if_t node) {
  neo_allocator_free(allocator, node->condition);
  neo_allocator_free(allocator, node->alternate);
  neo_allocator_free(allocator, node->consequent);
  neo_allocator_free(allocator, node->node.scope);
}
static void neo_ast_statement_if_resolve_closure(neo_allocator_t allocator,
                                                 neo_ast_statement_if_t self,
                                                 neo_list_t closure) {
  self->condition->resolve_closure(allocator, self->condition, closure);
  if (self->consequent) {
    self->consequent->resolve_closure(allocator, self->consequent, closure);
  }
  if (self->alternate) {
    self->alternate->resolve_closure(allocator, self->alternate, closure);
  }
}
static void neo_ast_statement_if_write(neo_allocator_t allocator,
                                       neo_write_context_t ctx,
                                       neo_ast_statement_if_t self) {
  TRY(self->condition->write(allocator, ctx, self->condition)) { return; }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_JFALSE);
  size_t alternate = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  TRY(self->alternate->write(allocator, ctx, self->alternate)) { return; }
  if (self->consequent) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_JMP);
    size_t end = neo_buffer_get_size(ctx->program->codes);
    neo_program_add_address(allocator, ctx->program, 0);
    neo_program_set_current(ctx->program, alternate);
    TRY(self->consequent->write(allocator, ctx, self->consequent)) { return; }
    neo_program_set_current(ctx->program, end);
  } else {
    neo_program_set_current(ctx->program, alternate);
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
}
static neo_any_t neo_serialize_ast_statement_if(neo_allocator_t allocator,
                                                neo_ast_statement_if_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_STATEMENT_FOR"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "condition",
              neo_ast_node_serialize(allocator, node->condition));
  neo_any_set(variable, "alternate",
              neo_ast_node_serialize(allocator, node->alternate));
  neo_any_set(variable, "consequent",
              neo_ast_node_serialize(allocator, node->consequent));
  return variable;
}
static neo_ast_statement_if_t
neo_create_ast_statement_if(neo_allocator_t allocator) {
  neo_ast_statement_if_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_statement_if_t),
                          neo_ast_statement_if_dispose);
  node->node.type = NEO_NODE_TYPE_STATEMENT_IF;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_statement_if;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_statement_if_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_statement_if_write;
  node->condition = NULL;
  node->alternate = NULL;
  node->consequent = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_statement_if(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_statement_if_t node = neo_create_ast_statement_if(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "if")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '(') {
    THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  node->condition = TRY(neo_ast_read_expression(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->condition) {
    THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ')') {
    THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  node->alternate = TRY(neo_ast_read_statement(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->alternate) {
    goto onerror;
  }
  neo_position_t cur = current;
  SKIP_ALL(allocator, file, &cur, onerror);
  token = neo_read_identify_token(allocator, file, &cur);
  if (token && neo_location_is(token->location, "else")) {
    current = cur;
    SKIP_ALL(allocator, file, &current, onerror);
    node->consequent = TRY(neo_ast_read_statement(allocator, file, &current)) {
      goto onerror;
    }
    if (!node->condition) {
      THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
      goto onerror;
    }
  }
  neo_allocator_free(allocator, token);
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  neo_allocator_free(allocator, token);
  return NULL;
}