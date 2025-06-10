#include "compiler/ast/statement_for_await_of.h"
#include "compiler/asm.h"
#include "compiler/ast/declaration_variable.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/node.h"
#include "compiler/ast/pattern_array.h"
#include "compiler/ast/pattern_object.h"
#include "compiler/ast/statement.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/location.h"
#include "core/variable.h"
#include <stdio.h>
static void
neo_ast_statement_for_await_of_dispose(neo_allocator_t allocator,
                                       neo_ast_statement_for_await_of_t node) {
  neo_allocator_free(allocator, node->left);
  neo_allocator_free(allocator, node->right);
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->node.scope);
}
static void neo_ast_statement_for_await_of_resolve_closure(
    neo_allocator_t allocator, neo_ast_statement_for_await_of_t self,
    neo_list_t closure) {
  self->left->resolve_closure(allocator, self->left, closure);
  self->right->resolve_closure(allocator, self->right, closure);
  self->body->resolve_closure(allocator, self->body, closure);
}
static void
neo_ast_statement_for_await_of_write(neo_allocator_t allocator,
                                     neo_write_context_t ctx,
                                     neo_ast_statement_for_await_of_t self) {
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_LABEL);
  neo_program_add_string(allocator, ctx->program, "");
  size_t labeladdr = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  TRY(self->right->write(allocator, ctx, self->right)) { return; }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_ITERATOR);
  size_t begin = neo_buffer_get_size(ctx->program->codes);
  neo_writer_push_scope(allocator, ctx, self->node.scope);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_NEXT);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_AWAIT);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_JTRUE);
  size_t address = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  if (self->left->type == NEO_NODE_TYPE_IDENTIFIER) {
    char *name = neo_location_get(allocator, self->left->location);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  } else {
    TRY(self->left->write(allocator, ctx, self->left)) { return; }
  }
  TRY(self->body->write(allocator, ctx, self->body)) { return; }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_JMP);
  neo_program_add_address(allocator, ctx->program, begin);
  neo_program_set_current(ctx->program, address);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  neo_writer_pop_scope(allocator, ctx, self->node.scope);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  neo_program_set_current(ctx->program, labeladdr);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP_LABEL);
}
static neo_variable_t neo_serialize_ast_statement_for_await_of(
    neo_allocator_t allocator, neo_ast_statement_for_await_of_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_STATEMENT_FOR_AWAIT_OF"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "text",
                   neo_ast_node_source_serialize(allocator, &node->node));
  neo_variable_set(variable, "left",
                   neo_ast_node_serialize(allocator, node->left));
  neo_variable_set(variable, "right",
                   neo_ast_node_serialize(allocator, node->right));
  neo_variable_set(variable, "body",
                   neo_ast_node_serialize(allocator, node->body));
  switch (node->kind) {
  case NEO_AST_DECLARATION_VAR:
    neo_variable_set(
        variable, "kind",
        neo_create_variable_string(allocator, "NEO_AST_DECLARATION_VAR"));
    break;
  case NEO_AST_DECLARATION_CONST:
    neo_variable_set(
        variable, "kind",
        neo_create_variable_string(allocator, "NEO_AST_DECLARATION_CONST"));
    break;
  case NEO_AST_DECLARATION_LET:
    neo_variable_set(
        variable, "kind",
        neo_create_variable_string(allocator, "NEO_AST_DECLARATION_LET"));
    break;
  case NEO_AST_DECLARATION_NONE:
    neo_variable_set(
        variable, "kind",
        neo_create_variable_string(allocator, "NEO_AST_DECLARATION_NONE"));
    break;
  }
  return variable;
}
static neo_ast_statement_for_await_of_t
neo_create_ast_statement_for_await_of(neo_allocator_t allocator) {
  neo_ast_statement_for_await_of_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_for_await_of);
  node->node.type = NEO_NODE_TYPE_STATEMENT_FOR_AWAIT_OF;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_statement_for_await_of;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_statement_for_await_of_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_statement_for_await_of_write;
  node->left = NULL;
  node->right = NULL;
  node->body = NULL;
  node->kind = NEO_AST_DECLARATION_NONE;
  return node;
}
neo_ast_node_t neo_ast_read_statement_for_await_of(neo_allocator_t allocator,
                                                   const char *file,
                                                   neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_statement_for_await_of_t node =
      neo_create_ast_statement_for_await_of(allocator);
  neo_token_t token = NULL;
  neo_compile_scope_t scope = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "for")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "await")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '(') {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  scope = neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_BLOCK);
  SKIP_ALL(allocator, file, &current, onerror);
  neo_position_t cur = current;
  token = neo_read_identify_token(allocator, file, &cur);
  if (token && neo_location_is(token->location, "const")) {
    node->kind = NEO_AST_DECLARATION_CONST;
    current = cur;
    SKIP_ALL(allocator, file, &current, onerror);
  } else if (token && neo_location_is(token->location, "let")) {
    node->kind = NEO_AST_DECLARATION_LET;
    current = cur;
    SKIP_ALL(allocator, file, &current, onerror);
  } else if (token && neo_location_is(token->location, "var")) {
    node->kind = NEO_AST_DECLARATION_VAR;
    current = cur;
    SKIP_ALL(allocator, file, &current, onerror);
  } else {
    node->kind = NEO_AST_DECLARATION_NONE;
  }
  neo_allocator_free(allocator, token);
  node->left = TRY(neo_ast_read_pattern_object(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->left) {
    node->left = TRY(neo_ast_read_pattern_array(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->left) {
    node->left = TRY(neo_ast_read_identifier(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->left) {
    goto onerror;
  }
  if (node->left->type == NEO_NODE_TYPE_IDENTIFIER) {
    switch (node->kind) {
    case NEO_AST_DECLARATION_VAR:
      TRY(neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                   node->left, NEO_COMPILE_VARIABLE_VAR)) {
        goto onerror;
      };
      break;
    case NEO_AST_DECLARATION_CONST:
      TRY(neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                   node->left, NEO_COMPILE_VARIABLE_CONST)) {
        goto onerror;
      };
      break;
    case NEO_AST_DECLARATION_LET:
      TRY(neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                   node->left, NEO_COMPILE_VARIABLE_LET)) {
        goto onerror;
      };
      break;
    case NEO_AST_DECLARATION_NONE:
      break;
    }
  }
  SKIP_ALL(allocator, file, &current, onerror);
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "of")) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->right = TRY(neo_ast_read_expression(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->right) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ')') {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
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
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
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
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}
