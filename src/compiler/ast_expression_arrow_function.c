#include "compiler/asm.h"
#include "compiler/ast_expression.h"
#include "compiler/ast_expression_arrow_function.h"
#include "compiler/ast_function_argument.h"
#include "compiler/ast_function_body.h"
#include "compiler/ast_identifier.h"
#include "compiler/ast_node.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/buffer.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include <stdbool.h>
#include <stdio.h>


static void neo_ast_expression_arrow_function_dispose(
    neo_allocator_t allocator, neo_ast_expression_arrow_function_t node) {
  neo_allocator_free(allocator, node->closure);
  neo_allocator_free(allocator, node->arguments);
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->node.scope);
  neo_allocator_free(allocator, node->closure);
}

static void neo_ast_expression_arrow_resolve_closure(
    neo_allocator_t allocator, neo_ast_expression_arrow_function_t self,
    neo_list_t closure) {

  neo_compile_scope_t current = neo_compile_scope_set(self->node.scope);
  for (neo_list_node_t it = neo_list_get_first(self->arguments);
       it != neo_list_get_tail(self->arguments); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
  neo_compile_scope_set(current);
  for (neo_list_node_t it = neo_list_get_first(self->closure);
       it != neo_list_get_tail(self->closure); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
}

static neo_any_t neo_serialize_ast_expression_arrow_function(
    neo_allocator_t allocator, neo_ast_expression_arrow_function_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator,
                                    "NEO_NODE_TYPE_EXPRESSION_ARROW_FUNCTION"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "arguments",
              neo_ast_node_list_serialize(allocator, node->arguments));
  neo_any_set(variable, "body", neo_ast_node_serialize(allocator, node->body));
  neo_any_set(variable, "closure",
              neo_ast_node_list_serialize(allocator, node->closure));
  neo_any_set(variable, "async",
              neo_create_any_boolean(allocator, node->async));
  return variable;
}
static void neo_ast_expression_arrow_function_write(
    neo_allocator_t allocator, neo_write_context_t ctx,
    neo_ast_expression_arrow_function_t self) {
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_JMP);
  size_t endaddr = neo_buffer_get_size(ctx->program->codes);
  neo_js_program_add_address(allocator, ctx->program, 0);
  size_t begin = neo_buffer_get_size(ctx->program->codes);
  neo_writer_push_scope(allocator, ctx, self->node.scope);
  if (neo_list_get_size(self->arguments)) {
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_LOAD);
    neo_js_program_add_string(allocator, ctx->program, "arguments");
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_ITERATOR);
    for (neo_list_node_t it = neo_list_get_first(self->arguments);
         it != neo_list_get_tail(self->arguments);
         it = neo_list_node_next(it)) {
      neo_ast_node_t argument = neo_list_node_get(it);
      if (argument->type == NEO_NODE_TYPE_IDENTIFIER) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_NEXT);
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_RESOLVE_NEXT);
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
        char *name = neo_location_get(allocator, argument->location);
        neo_js_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      } else {
        argument->write(allocator, ctx, argument);
      }
    }
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  }
  bool is_async = ctx->is_async;
  ctx->is_async = self->async;
  bool is_generator = ctx->is_generator;
  ctx->is_generator = false;
  self->body->write(allocator, ctx, self->body);
  if (self->body->type != NEO_NODE_TYPE_FUNCTION_BODY) {
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_RET);
  }
  ctx->is_generator = is_generator;
  ctx->is_async = is_async;
  neo_writer_pop_scope(allocator, ctx, self->node.scope);
  neo_js_program_set_current(ctx->program, endaddr);
  if (self->async) {
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_ASYNC_LAMBDA);
  } else {
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_LAMBDA);
  }
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SET_ADDRESS);
  neo_js_program_add_address(allocator, ctx->program, begin);
  char *source = neo_location_get(allocator, self->node.location);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SET_SOURCE);
  neo_js_program_add_string(allocator, ctx->program, source);
  neo_allocator_free(allocator, source);
  for (neo_list_node_t it = neo_list_get_first(self->closure);
       it != neo_list_get_tail(self->closure); it = neo_list_node_next(it)) {
    neo_ast_node_t node = neo_list_node_get(it);
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SET_CLOSURE);
    char *name = neo_location_get(allocator, node->location);
    neo_js_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  }
}
static neo_ast_expression_arrow_function_t
neo_create_ast_expression_arrow_function(neo_allocator_t allocator) {
  neo_ast_expression_arrow_function_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_expression_arrow_function_t),
      neo_ast_expression_arrow_function_dispose);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_ARROW_FUNCTION;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_expression_arrow_function;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_expression_arrow_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_expression_arrow_function_write;
  neo_list_initialize_t initialize = {true};

  node->arguments = neo_create_list(allocator, &initialize);
  node->closure = neo_create_list(allocator, NULL);
  node->body = NULL;
  node->async = false;
  return node;
}

neo_ast_node_t neo_ast_read_expression_arrow_function(
    neo_allocator_t allocator, const char *file, neo_position_t *position) {
  neo_ast_node_t error = NULL;
  neo_position_t current = *position;
  neo_ast_expression_arrow_function_t node = NULL;
  neo_token_t token = NULL;
  neo_compile_scope_t scope = NULL;
  node = neo_create_ast_expression_arrow_function(allocator);
  token = neo_read_identify_token(allocator, file, &current);
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (token) {
    if (neo_location_is(token->location, "async")) {
      node->async = true;
    } else {
      current = token->location.begin;
    }
    neo_allocator_free(allocator, token);
  }

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }

  if (*current.offset == '(') {
    current.offset++;
    current.column++;
    scope = neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_FUNCTION, false,
                                   node->async);
    if (*current.offset != ')') {
      for (;;) {
        neo_ast_node_t argument =
            neo_ast_read_function_argument(allocator, file, &current);
        if (!argument) {
          goto onerror;
        }
        NEO_CHECK_NODE(argument, error, onerror);
        neo_list_push(node->arguments, argument);
        argument->resolve_closure(allocator, argument, node->closure);

        error = neo_skip_all(allocator, file, &current);
        if (error) {
          goto onerror;
        }
        if (*current.offset == ')') {
          break;
        }
        if (*current.offset != ',') {
          goto onerror;
        }
        if (((neo_ast_function_argument_t)argument)->identifier->type ==
            NEO_NODE_TYPE_PATTERN_REST) {
          goto onerror;
        }
        current.offset++;
        current.column++;

        error = neo_skip_all(allocator, file, &current);
        if (error) {
          goto onerror;
        }
      }
    }

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    if (*current.offset != ')') {
      goto onerror;
    }
    current.offset++;
    current.column++;
  } else {
    scope = neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_FUNCTION, false,
                                   node->async);
    neo_ast_node_t argument =
        neo_ast_read_identifier(allocator, file, &current);
    if (!argument) {
      goto onerror;
    }
    NEO_CHECK_NODE(argument, error, onerror);
    neo_list_push(node->arguments, argument);
    error = neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                     argument, NEO_COMPILE_VARIABLE_VAR);
    if (error) {
      goto onerror;
    };
  }

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset != '=' || *(current.offset + 1) != '>') {
    goto onerror;
  } else {
    current.offset += 2;
    current.column += 2;
  }

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }

  if (*current.offset == '{') {
    node->body = neo_ast_read_function_body(allocator, file, &current);
  } else {
    node->body = neo_ast_read_expression_2(allocator, file, &current);
  }

  if (!node->body) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  NEO_CHECK_NODE(node->body, error, onerror);
  node->body->resolve_closure(allocator, node->body, node->closure);
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