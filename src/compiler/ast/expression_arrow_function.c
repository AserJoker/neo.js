#include "compiler/ast/expression_arrow_function.h"
#include "compiler/asm.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/function_argument.h"
#include "compiler/ast/function_body.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
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

static neo_variable_t neo_serialize_ast_expression_arrow_function(
    neo_allocator_t allocator, neo_ast_expression_arrow_function_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_EXPRESSION_ARROW_FUNCTION"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "arguments",
                   neo_ast_node_list_serialize(allocator, node->arguments));
  neo_variable_set(variable, "body",
                   neo_ast_node_serialize(allocator, node->body));
  neo_variable_set(variable, "closure",
                   neo_ast_node_list_serialize(allocator, node->closure));
  neo_variable_set(variable, "async",
                   neo_create_variable_boolean(allocator, node->async));
  return variable;
}
static void neo_ast_expression_arrow_function_write(
    neo_allocator_t allocator, neo_write_context_t ctx,
    neo_ast_expression_arrow_function_t self) {
  neo_program_add_code(allocator, ctx->program, NEO_ASM_JMP);
  size_t endaddr = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  size_t begin = neo_buffer_get_size(ctx->program->codes);
  neo_writer_push_scope(allocator, ctx, self->node.scope);
  if (neo_list_get_size(self->arguments)) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_LOAD);
    neo_program_add_string(allocator, ctx->program, "arguments");
    neo_program_add_code(allocator, ctx->program, NEO_ASM_ITERATOR);
    for (neo_list_node_t it = neo_list_get_first(self->arguments);
         it != neo_list_get_tail(self->arguments);
         it = neo_list_node_next(it)) {
      neo_ast_node_t argument = neo_list_node_get(it);
      TRY(argument->write(allocator, ctx, argument)) { return; }
    }
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  }
  TRY(self->body->write(allocator, ctx, self->body)) { return; }
  if (self->body->type != NEO_NODE_TYPE_FUNCTION_BODY) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_RET);
  }
  neo_writer_pop_scope(allocator, ctx, self->node.scope);
  neo_program_set_current(ctx->program, endaddr);
  if (self->async) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_ASYNC_LAMBDA);
  } else {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_LAMBDA);
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_ADDRESS);
  neo_program_add_address(allocator, ctx->program, begin);
  char *source = neo_location_get(allocator, self->node.location);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_SOURCE);
  neo_program_add_string(allocator, ctx->program, source);
  neo_allocator_free(allocator, source);
  for (neo_list_node_t it = neo_list_get_first(self->closure);
       it != neo_list_get_tail(self->closure); it = neo_list_node_next(it)) {
    neo_ast_node_t node = neo_list_node_get(it);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_CLOSURE);
    char *name = neo_location_get(allocator, node->location);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  }
}
static neo_ast_expression_arrow_function_t
neo_create_ast_expression_arrow_function(neo_allocator_t allocator) {
  neo_ast_expression_arrow_function_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_arrow_function);
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
  neo_position_t current = *position;
  neo_ast_expression_arrow_function_t node = NULL;
  neo_token_t token = NULL;
  neo_compile_scope_t scope = NULL;
  node = neo_create_ast_expression_arrow_function(allocator);

  token = TRY(neo_read_identify_token(allocator, file, &current)) {
    goto onerror;
  };

  if (token) {
    if (neo_location_is(token->location, "async")) {
      node->async = true;
    } else {
      current = token->location.begin;
    }
    neo_allocator_free(allocator, token);
  }

  SKIP_ALL(allocator, file, &current, onerror);

  if (*current.offset == '(') {
    current.offset++;
    current.column++;
    scope = neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_FUNCTION);
    if (*current.offset != ')') {
      for (;;) {
        neo_ast_node_t argument =
            TRY(neo_ast_read_function_argument(allocator, file, &current)) {
          goto onerror;
        }
        if (!argument) {
          THROW("Invalid or unexpected token \n  at %s:%d:%d", file,
                current.line, current.column);
          goto onerror;
        }
        neo_list_push(node->arguments, argument);
        argument->resolve_closure(allocator, argument, node->closure);
        SKIP_ALL(allocator, file, &current, onerror);
        if (*current.offset == ')') {
          break;
        }
        if (*current.offset != ',') {
          goto onerror;
        }
        if (((neo_ast_function_argument_t)argument)->identifier->type ==
            NEO_NODE_TYPE_PATTERN_REST) {
          THROW("Invalid or unexpected token \n  at %s:%d:%d", file,
                current.line, current.column);
          goto onerror;
        }
        current.offset++;
        current.column++;
        SKIP_ALL(allocator, file, &current, onerror);
      }
    }
    SKIP_ALL(allocator, file, &current, onerror);
    if (*current.offset != ')') {
      goto onerror;
    }
    current.offset++;
    current.column++;
  } else {
    scope = neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_FUNCTION);
    neo_ast_node_t argument =
        TRY(neo_ast_read_identifier(allocator, file, &current)) {
      goto onerror;
    }
    if (!argument) {
      goto onerror;
    }
    neo_list_push(node->arguments, argument);
    TRY(neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                 argument, NEO_COMPILE_VARIABLE_VAR)) {
      goto onerror;
    };
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '=' || *(current.offset + 1) != '>') {
    goto onerror;
  } else {
    current.offset += 2;
    current.column += 2;
  }
  SKIP_ALL(allocator, file, &current, onerror);

  if (*current.offset == '{') {
    node->body = TRY(neo_ast_read_function_body(allocator, file, &current)) {
      goto onerror;
    }
  } else {
    node->body = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
      goto onerror;
    };
  }

  if (!node->body) {
    THROW("Invalid or unexpected token \n  at %s:%d:%d", file, current.line,
          current.column);
    goto onerror;
  }
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
  return NULL;
}