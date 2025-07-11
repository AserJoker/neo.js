#include "compiler/ast/statement_do_while.h"
#include "compiler/asm.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/node.h"
#include "compiler/ast/statement.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/variable.h"
#include <stdio.h>

static void
neo_ast_statement_do_while_dispose(neo_allocator_t allocator,
                                   neo_ast_statement_do_while_t node) {
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->condition);
  neo_allocator_free(allocator, node->node.scope);
}
static void
neo_ast_statement_do_while_resolve_closure(neo_allocator_t allocator,
                                           neo_ast_statement_do_while_t self,
                                           neo_list_t closure) {
  neo_compile_scope_t scope = neo_compile_scope_set(self->node.scope);
  self->condition->resolve_closure(allocator, self->condition, closure);
  self->body->resolve_closure(allocator, self->body, closure);
  neo_compile_scope_pop(self->node.scope);
}
static void
neo_ast_statement_do_while_write(neo_allocator_t allocator,
                                 neo_write_context_t ctx,
                                 neo_ast_statement_do_while_t self) {
  wchar_t *label = ctx->label;
  ctx->label = NULL;
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_BREAK_LABEL);
  neo_program_add_string(allocator, ctx->program, label ? label : L"");
  size_t breakaddr = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_CONTINUE_LABEL);
  neo_program_add_string(allocator, ctx->program, label ? label : L"");
  size_t continueaddr = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_UNDEFINED);
  size_t address = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  TRY(self->body->write(allocator, ctx, self->body)) { return; }
  neo_program_set_current(ctx->program, continueaddr);
  TRY(self->condition->write(allocator, ctx, self->condition)) { return; }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_JTRUE);
  neo_program_add_address(allocator, ctx->program, address);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  neo_program_set_current(ctx->program, continueaddr);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP_LABEL);
  neo_program_set_current(ctx->program, breakaddr);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP_LABEL);
  ctx->label = label;
}
static neo_variable_t
neo_serialize_ast_statement_do_while(neo_allocator_t allocator,
                                     neo_ast_statement_do_while_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, L"type",
                   neo_create_variable_string(
                       allocator, L"NEO_NODE_TYPE_STATEMENT_DO_WHILE"));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, L"body",
                   neo_ast_node_serialize(allocator, node->body));
  neo_variable_set(variable, L"condition",
                   neo_ast_node_serialize(allocator, node->condition));
  return variable;
}
static neo_ast_statement_do_while_t
neo_create_ast_statement_do_while(neo_allocator_t allocator) {
  neo_ast_statement_do_while_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_do_while);
  node->node.type = NEO_NODE_TYPE_STATEMENT_DO_WHILE;

  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_statement_do_while;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_statement_do_while_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_statement_do_while_write;
  node->body = NULL;
  node->condition = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_statement_do_while(neo_allocator_t allocator,
                                               const wchar_t *file,
                                               neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_statement_do_while_t node =
      neo_create_ast_statement_do_while(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "do")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->body = TRY(neo_ast_read_statement(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->body) {
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "while")) {
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '(') {
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
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
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ')') {
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
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