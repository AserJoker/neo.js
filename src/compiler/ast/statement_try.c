#include "compiler/ast/statement_try.h"
#include "compiler/asm.h"
#include "compiler/ast/node.h"
#include "compiler/ast/statement_block.h"
#include "compiler/ast/try_catch.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/variable.h"
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
    self->catch->resolve_closure(allocator, self->finally, closure);
  }
}
static void neo_ast_statement_try_write(neo_allocator_t allocator,
                                        neo_write_context_t ctx,
                                        neo_ast_statement_try_t self) {
  neo_program_add_code(allocator, ctx->program, NEO_ASM_TRY_BEGIN);
  size_t catchaddr = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  size_t deferaddr = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  TRY(self->body->write(allocator, ctx, self->body)) { return; }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_JMP);
  size_t end = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  if (self->catch) {
    neo_program_set_current(ctx->program, catchaddr);
    TRY(self->catch->write(allocator, ctx, self->catch)) { return; }
  }
  if (self->finally) {
    neo_program_set_current(ctx->program, deferaddr);
    neo_program_set_current(ctx->program, end);
    TRY(self->finally->write(allocator, ctx, self->finally)) { return; }
  } else {
    neo_program_set_current(ctx->program, end);
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_TRY_END);
}
static neo_variable_t
neo_serialize_ast_statement_try(neo_allocator_t allocator,
                                neo_ast_statement_try_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_STATEMENT_TRY"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "body",
                   neo_ast_node_serialize(allocator, node->body));
  neo_variable_set(variable, "catch",
                   neo_ast_node_serialize(allocator, node->catch));
  neo_variable_set(variable, "finally",
                   neo_ast_node_serialize(allocator, node->finally));
  return variable;
}
static neo_ast_statement_try_t
neo_create_ast_statement_try(neo_allocator_t allocator) {
  neo_ast_statement_try_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_try);
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
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "try")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->body = TRY(neo_ast_read_statement_block(allocator, file, &current)) {
    goto onerror;
  };
  if (!node->body) {
    THROW("Invalid or unexpected token \n  at %s:%d:%d", file, current.line,
          current.column);
    goto onerror;
  }
  neo_position_t cur = current;
  SKIP_ALL(allocator, file, &cur, onerror);
  node->catch = TRY(neo_ast_read_try_catch(allocator, file, &cur)) {
    goto onerror;
  };
  if (node->catch) {
    current = cur;
  }
  SKIP_ALL(allocator, file, &cur, onerror);
  token = neo_read_identify_token(allocator, file, &cur);
  if (token && neo_location_is(token->location, "finally")) {
    SKIP_ALL(allocator, file, &cur, onerror);
    node->finally = TRY(neo_ast_read_statement_block(allocator, file, &cur)) {
      goto onerror;
    };
    if (!node->finally) {
      THROW("Invalid or unexpected token \n  at %s:%d:%d", file, cur.line,
            cur.column);
      goto onerror;
    }
    current = cur;
  }
  neo_allocator_free(allocator, token);
  if (!node->catch && !node->finally) {
    THROW("Invalid or unexpected token \n  at %s:%d:%d", file, current.line,
          current.column);
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