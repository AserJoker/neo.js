#include "compiler/ast/statement_try.h"
#include "compiler/asm.h"
#include "compiler/ast/node.h"
#include "compiler/ast/statement_block.h"
#include "compiler/ast/try_catch.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/buffer.h"
#include <stdio.h>

static void neo_ast_statement_try_dispose(neo_allocator_t allocator,
                                          neo_ast_statement_try_t node) {
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->catch);
  neo_allocator_free(allocator, node->finally);
  neo_allocator_free(allocator, node->node.scope);
}
static void neo_ast_statement_try_resolve_closure(neo_allocator_t allocator,
                                                  neo_ast_statement_try_t self,
                                                  neo_list_t closure) {
  self->body->resolve_closure(allocator, self->body, closure);
  if (self->catch) {
    self->catch->resolve_closure(allocator, self->catch, closure);
  }
  if (self->finally) {
    self->finally->resolve_closure(allocator, self->finally, closure);
  }
}
static void neo_ast_statement_try_write(neo_allocator_t allocator,
                                        neo_write_context_t ctx,
                                        neo_ast_statement_try_t self) {
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_TRY_BEGIN);
  size_t catchaddr = neo_buffer_get_size(ctx->program->codes);
  neo_js_program_add_address(allocator, ctx->program, 0);
  size_t deferaddr = neo_buffer_get_size(ctx->program->codes);
  neo_js_program_add_address(allocator, ctx->program, 0);
  self->body->write(allocator, ctx, self->body);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_TRY_END);
  if (self->catch) {
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_JMP);
    size_t catchend = neo_buffer_get_size(ctx->program->codes);
    neo_js_program_add_address(allocator, ctx->program, 0);
    neo_js_program_set_current(ctx->program, catchaddr);
    self->catch->write(allocator, ctx, self->catch);
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_TRY_END);
    neo_js_program_set_current(ctx->program, catchend);
  }
  if (self->finally) {
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_JMP);
    size_t finallyend = neo_buffer_get_size(ctx->program->codes);
    neo_js_program_add_address(allocator, ctx->program, 0);
    neo_js_program_set_current(ctx->program, deferaddr);
    self->finally->write(allocator, ctx, self->finally);
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_TRY_END);
    neo_js_program_set_current(ctx->program, finallyend);
  }
}
static neo_any_t neo_serialize_ast_statement_try(neo_allocator_t allocator,
                                                 neo_ast_statement_try_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_STATEMENT_TRY"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "body", neo_ast_node_serialize(allocator, node->body));
  neo_any_set(variable, "catch",
              neo_ast_node_serialize(allocator, node->catch));
  neo_any_set(variable, "finally",
              neo_ast_node_serialize(allocator, node->finally));
  return variable;
}
static neo_ast_statement_try_t
neo_create_ast_statement_try(neo_allocator_t allocator) {
  neo_ast_statement_try_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_statement_try_t),
                          neo_ast_statement_try_dispose);
  node->node.type = NEO_NODE_TYPE_STATEMENT_TRY;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_statement_try;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_statement_try_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_statement_try_write;
  node->body = NULL;
  node->catch = NULL;
  node->finally = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_statement_try(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_statement_try_t node = neo_create_ast_statement_try(allocator);
  neo_ast_node_t error = NULL;
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (!token || !neo_location_is(token->location, "try")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  node->body = neo_ast_read_statement_block(allocator, file, &current);
  if (!node->body) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  NEO_CHECK_NODE(node->body, error, onerror);
  neo_position_t cur = current;
  error = neo_skip_all(allocator, file, &cur);
  if (error) {
    goto onerror;
  }
  node->catch = neo_ast_read_try_catch(allocator, file, &cur);
  if (node->catch) {
    NEO_CHECK_NODE(node->catch, error, onerror);
    current = cur;
  }
  error = neo_skip_all(allocator, file, &cur);
  if (error) {
    goto onerror;
  }
  token = neo_read_identify_token(allocator, file, &cur);
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (token && neo_location_is(token->location, "finally")) {
    error = neo_skip_all(allocator, file, &cur);
    if (error) {
      goto onerror;
    }
    node->finally = neo_ast_read_statement_block(allocator, file, &cur);
    if (!node->finally) {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, cur.line, cur.column);
      goto onerror;
    }
    NEO_CHECK_NODE(node->finally, error, onerror);
    current = cur;
  }
  neo_allocator_free(allocator, token);
  if (!node->catch && !node->finally) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
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
  return error;
}