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
#include "core/any.h"
#include "core/buffer.h"
#include "core/location.h"
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
  neo_compile_scope_t scope = neo_compile_scope_set(self->node.scope);
  self->left->resolve_closure(allocator, self->left, closure);
  self->right->resolve_closure(allocator, self->right, closure);
  self->body->resolve_closure(allocator, self->body, closure);
  neo_compile_scope_set(scope);
}
static void
neo_ast_statement_for_await_of_write(neo_allocator_t allocator,
                                     neo_write_context_t ctx,
                                     neo_ast_statement_for_await_of_t self) {
  char *label = ctx->label;
  ctx->label = NULL;
  self->right->write(allocator, ctx, self->right);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_ASYNC_ITERATOR);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_BREAK_LABEL);
  neo_js_program_add_string(allocator, ctx->program, label ? label : "");
  size_t breakaddr = neo_buffer_get_size(ctx->program->codes);
  neo_js_program_add_address(allocator, ctx->program, 0);
  size_t begin = neo_buffer_get_size(ctx->program->codes);
  neo_writer_push_scope(allocator, ctx, self->node.scope);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_NEXT);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_AWAIT);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_RESOLVE_NEXT);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_JTRUE);
  size_t address = neo_buffer_get_size(ctx->program->codes);
  neo_js_program_add_address(allocator, ctx->program, 0);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  if (self->left->type == NEO_NODE_TYPE_IDENTIFIER) {
    char *name = neo_location_get(allocator, self->left->location);
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
    neo_js_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  } else {
    self->left->write(allocator, ctx, self->left);
  }
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_CONTINUE_LABEL);
  neo_js_program_add_string(allocator, ctx->program, label ? label : "");
  size_t continueaddr = neo_buffer_get_size(ctx->program->codes);
  neo_js_program_add_address(allocator, ctx->program, 0);
  self->body->write(allocator, ctx, self->body);
  neo_js_program_set_current(ctx->program, continueaddr);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP_LABEL);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_JMP);
  neo_js_program_add_address(allocator, ctx->program, begin);
  neo_js_program_set_current(ctx->program, address);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  neo_writer_pop_scope(allocator, ctx, self->node.scope);
  neo_js_program_set_current(ctx->program, breakaddr);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP_LABEL);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  ctx->label = NULL;
}
static neo_any_t neo_serialize_ast_statement_for_await_of(
    neo_allocator_t allocator, neo_ast_statement_for_await_of_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(
      variable, "type",
      neo_create_any_string(allocator, "NEO_NODE_TYPE_STATEMENT_FOR_AWAIT_OF"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "text",
              neo_ast_node_source_serialize(allocator, &node->node));
  neo_any_set(variable, "left", neo_ast_node_serialize(allocator, node->left));
  neo_any_set(variable, "right",
              neo_ast_node_serialize(allocator, node->right));
  neo_any_set(variable, "body", neo_ast_node_serialize(allocator, node->body));
  switch (node->kind) {
  case NEO_AST_DECLARATION_VAR:
    neo_any_set(variable, "kind",
                neo_create_any_string(allocator, "NEO_AST_DECLARATION_VAR"));
    break;
  case NEO_AST_DECLARATION_CONST:
    neo_any_set(variable, "kind",
                neo_create_any_string(allocator, "NEO_AST_DECLARATION_CONST"));
    break;
  case NEO_AST_DECLARATION_LET:
    neo_any_set(variable, "kind",
                neo_create_any_string(allocator, "NEO_AST_DECLARATION_LET"));
    break;
  case NEO_AST_DECLARATION_NONE:
    neo_any_set(variable, "kind",
                neo_create_any_string(allocator, "NEO_AST_DECLARATION_NONE"));
    break;
  case NEO_AST_DECLARATION_USING:
    neo_any_set(variable, "kind",
                neo_create_any_string(allocator, "NEO_AST_DECLARATION_USING"));
    break;
  case NEO_AST_DECLARATION_AWAIT_USING:
    neo_any_set(
        variable, "kind",
        neo_create_any_string(allocator, "NEO_AST_DECLARATION_AWAIT_USING"));
    break;
  }
  return variable;
}
static neo_ast_statement_for_await_of_t
neo_create_ast_statement_for_await_of(neo_allocator_t allocator) {
  neo_ast_statement_for_await_of_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_statement_for_await_of_t),
      neo_ast_statement_for_await_of_dispose);
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
  neo_ast_node_t error = NULL;
  neo_token_t token = NULL;
  neo_compile_scope_t scope = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "for")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "await")) {
    goto onerror;
  }
  if (!neo_compile_scope_is_async()) {
    error = neo_create_error_node(allocator,
                                  "await only used in generator context");
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
  scope =
      neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_BLOCK, false, false);

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  neo_position_t cur = current;
  token = neo_read_identify_token(allocator, file, &cur);
  if (neo_location_is(token->location, "await")) {

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    neo_allocator_free(allocator, token);
    token = neo_read_identify_token(allocator, file, &current);
    if (!token) {
      goto onerror;
    }
    if (neo_location_is(token->location, "using")) {
      node->kind = NEO_AST_DECLARATION_AWAIT_USING;
      current = cur;

      error = neo_skip_all(allocator, file, &current);
      if (error) {
        goto onerror;
      }
    } else {
      goto onerror;
    }
  } else if (token && neo_location_is(token->location, "const")) {
    node->kind = NEO_AST_DECLARATION_CONST;
    current = cur;

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
  } else if (token && neo_location_is(token->location, "let")) {
    node->kind = NEO_AST_DECLARATION_LET;
    current = cur;

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
  } else if (token && neo_location_is(token->location, "var")) {
    node->kind = NEO_AST_DECLARATION_VAR;
    current = cur;

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
  } else if (token && neo_location_is(token->location, "using")) {
    node->kind = NEO_AST_DECLARATION_USING;
    current = cur;

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
  } else {
    node->kind = NEO_AST_DECLARATION_NONE;
  }
  neo_allocator_free(allocator, token);
  node->left = neo_ast_read_pattern_object(allocator, file, &current);
  if (!node->left) {
    node->left = neo_ast_read_pattern_array(allocator, file, &current);
  }
  if (!node->left) {
    node->left = neo_ast_read_identifier(allocator, file, &current);
  }
  if (!node->left) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  NEO_CHECK_NODE(node->left, error, onerror);
  if (node->left->type == NEO_NODE_TYPE_IDENTIFIER) {
    switch (node->kind) {
    case NEO_AST_DECLARATION_VAR:
      error =
          neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                   node->left, NEO_COMPILE_VARIABLE_VAR);
      if (error) {
        goto onerror;
      }
      break;
    case NEO_AST_DECLARATION_CONST:
      error =
          neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                   node->left, NEO_COMPILE_VARIABLE_CONST);
      if (error) {
        goto onerror;
      }
      break;
    case NEO_AST_DECLARATION_LET:
      error =
          neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                   node->left, NEO_COMPILE_VARIABLE_LET);
      if (error) {
        goto onerror;
      }
      break;
    case NEO_AST_DECLARATION_NONE:
      break;
    case NEO_AST_DECLARATION_USING:
      error =
          neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                   node->left, NEO_COMPILE_VARIABLE_USING);
      if (error) {
        goto onerror;
      }
      break;
    case NEO_AST_DECLARATION_AWAIT_USING:
      error = neo_compile_scope_declar(
          allocator, neo_compile_scope_get_current(), node->left,
          NEO_COMPILE_VARIABLE_AWAIT_USING);
      if (error) {
        goto onerror;
      }
      break;
    }
  }

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "of")) {
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
  node->right = neo_ast_read_expression(allocator, file, &current);
  if (!node->right) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  NEO_CHECK_NODE(node->right, error, onerror);
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
  return error;
}
