#include "compiler/ast/statement_for.h"
#include "compiler/asm.h"
#include "compiler/ast/declaration_variable.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/node.h"
#include "compiler/ast/statement.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_statement_for_dispose(neo_allocator_t allocator,
                                          neo_ast_statement_for_t node) {
  neo_allocator_free(allocator, node->initialize);
  neo_allocator_free(allocator, node->condition);
  neo_allocator_free(allocator, node->after);
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->node.scope);
}
static void neo_ast_statement_for_resolve_closure(neo_allocator_t allocator,
                                                  neo_ast_statement_for_t self,
                                                  neo_list_t closure) {
  neo_compile_scope_t scope = neo_compile_scope_set(self->node.scope);
  self->initialize->resolve_closure(allocator, self->initialize, closure);
  self->condition->resolve_closure(allocator, self->condition, closure);
  self->after->resolve_closure(allocator, self->after, closure);
  self->body->resolve_closure(allocator, self->body, closure);
  neo_compile_scope_set(scope);
}

static void neo_ast_statement_for_write(neo_allocator_t allocator,
                                        neo_write_context_t ctx,
                                        neo_ast_statement_for_t self) {
  char *label = ctx->label;
  ctx->label = NULL;
  neo_writer_push_scope(allocator, ctx, self->node.scope);
  if (self->initialize) {
    TRY(self->initialize->write(allocator, ctx, self->initialize)) { return; }
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_BREAK_LABEL);
  neo_program_add_string(allocator, ctx->program, label ? label : "");
  size_t breakaddr = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  size_t begin = neo_buffer_get_size(ctx->program->codes);
  if (self->condition) {
    TRY(self->condition->write(allocator, ctx, self->condition)) { return; }
  } else {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_TRUE);
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_JFALSE);
  size_t end = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_CONTINUE_LABEL);
  neo_program_add_string(allocator, ctx->program, label ? label : "");
  size_t continueaddr = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  TRY(self->body->write(allocator, ctx, self->body)) { return; }
  neo_program_set_current(ctx->program, continueaddr);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP_LABEL);
  if (self->after) {
    TRY(self->after->write(allocator, ctx, self->after)) { return; }
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_JMP);
  neo_program_add_address(allocator, ctx->program, begin);
  neo_program_set_current(ctx->program, end);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  neo_program_set_current(ctx->program, breakaddr);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP_LABEL);
  neo_writer_pop_scope(allocator, ctx, self->node.scope);
  ctx->label = label;
}

static neo_variable_t
neo_serialize_ast_statement_for(neo_allocator_t allocator,
                                neo_ast_statement_for_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_STATEMENT_FOR"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "initialize",
                   neo_ast_node_serialize(allocator, node->initialize));
  neo_variable_set(variable, "condition",
                   neo_ast_node_serialize(allocator, node->condition));
  neo_variable_set(variable, "body",
                   neo_ast_node_serialize(allocator, node->body));
  return variable;
}
static neo_ast_statement_for_t
neo_create_ast_statement_for(neo_allocator_t allocator) {
  neo_ast_statement_for_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_statement_for_t),
                          neo_ast_statement_for_dispose);
  node->node.type = NEO_NODE_TYPE_STATEMENT_FOR;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_statement_for;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_statement_for_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_statement_for_write;
  node->initialize = NULL;
  node->condition = NULL;
  node->after = NULL;
  node->body = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_statement_for(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_statement_for_t node = neo_create_ast_statement_for(allocator);
  neo_token_t token = NULL;
  neo_compile_scope_t scope = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "for")) {
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
  scope =
      neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_BLOCK, false, false);
  node->initialize = TRY(neo_ast_read_expression(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->initialize) {
    node->initialize =
        TRY(neo_ast_read_declaration_variable(allocator, file, &current)) {
      goto onerror;
    }
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ';') {
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
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ';') {
    THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  node->after = TRY(neo_ast_read_expression(allocator, file, &current)) {
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
  node->body = TRY(neo_ast_read_statement(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->body) {
    THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  node->node.scope = neo_compile_scope_pop(scope);
  *position = current;
  return &node->node;
onerror:
  if (scope && !node->node.scope) {
    scope = neo_compile_scope_pop(scope);
    neo_allocator_free(allocator, scope);
  }
  neo_allocator_free(allocator, node);
  neo_allocator_free(allocator, token);
  return NULL;
}