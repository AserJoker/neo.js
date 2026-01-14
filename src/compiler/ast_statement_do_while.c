#include "neo.js/compiler/ast_statement_do_while.h"
#include "neo.js/compiler/asm.h"
#include "neo.js/compiler/ast_expression.h"
#include "neo.js/compiler/ast_node.h"
#include "neo.js/compiler/ast_statement.h"
#include "neo.js/compiler/program.h"
#include "neo.js/compiler/token.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/any.h"
#include "neo.js/core/buffer.h"
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
  neo_compile_scope_set(scope);
}
static void
neo_ast_statement_do_while_write(neo_allocator_t allocator,
                                 neo_write_context_t ctx,
                                 neo_ast_statement_do_while_t self) {
  char *label = ctx->label;
  ctx->label = NULL;
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_BREAK_LABEL);
  neo_js_program_add_string(allocator, ctx->program, label ? label : "");
  size_t breakaddr = neo_buffer_get_size(ctx->program->codes);
  neo_js_program_add_address(allocator, ctx->program, 0);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_CONTINUE_LABEL);
  neo_js_program_add_string(allocator, ctx->program, label ? label : "");
  size_t continueaddr = neo_buffer_get_size(ctx->program->codes);
  neo_js_program_add_address(allocator, ctx->program, 0);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_UNDEFINED);
  size_t address = neo_buffer_get_size(ctx->program->codes);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  self->body->write(allocator, ctx, self->body);
  neo_js_program_set_current(ctx->program, continueaddr);
  self->condition->write(allocator, ctx, self->condition);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_JTRUE);
  neo_js_program_add_address(allocator, ctx->program, address);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  neo_js_program_set_current(ctx->program, continueaddr);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP_LABEL);
  neo_js_program_set_current(ctx->program, breakaddr);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP_LABEL);
  ctx->label = label;
}
static neo_any_t
neo_serialize_ast_statement_do_while(neo_allocator_t allocator,
                                     neo_ast_statement_do_while_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(
      variable, "type",
      neo_create_any_string(allocator, "NEO_NODE_TYPE_STATEMENT_DO_WHILE"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "body", neo_ast_node_serialize(allocator, node->body));
  neo_any_set(variable, "condition",
              neo_ast_node_serialize(allocator, node->condition));
  return variable;
}
static neo_ast_statement_do_while_t
neo_create_ast_statement_do_while(neo_allocator_t allocator) {
  neo_ast_statement_do_while_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_statement_do_while_t),
      neo_ast_statement_do_while_dispose);
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
                                               const char *file,
                                               neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_statement_do_while_t node =
      neo_create_ast_statement_do_while(allocator);
  neo_ast_node_t error = NULL;
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (!token || !neo_location_is(token->location, "do")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  node->body = neo_ast_read_statement(allocator, file, &current);
  if (!node->body) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  NEO_CHECK_NODE(node->body, error, onerror);
  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  token = neo_read_identify_token(allocator, file, &current);
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (!token || !neo_location_is(token->location, "while")) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  neo_allocator_free(allocator, token);

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset != '(') {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  node->condition = neo_ast_read_expression(allocator, file, &current);
  if (!node->condition) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  NEO_CHECK_NODE(node->condition, error, onerror);
  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset != ')') {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
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
  return error;
}