#include "compiler/ast/object_method.h"
#include "compiler/asm.h"
#include "compiler/ast/function_argument.h"
#include "compiler/ast/function_body.h"
#include "compiler/ast/node.h"
#include "compiler/ast/object_key.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include <stdio.h>

static void neo_ast_object_method_dispose(neo_allocator_t allocator,
                                          neo_ast_object_method_t node) {
  neo_allocator_free(allocator, node->arguments);
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->name);
  neo_allocator_free(allocator, node->node.scope);
  neo_allocator_free(allocator, node->closure);
}
static void neo_ast_object_method_resolve_closure(neo_allocator_t allocator,
                                                  neo_ast_object_method_t self,
                                                  neo_list_t closure) {
  if (self->computed) {
    self->name->resolve_closure(allocator, self->name, closure);
  }
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
static void neo_ast_object_method_write(neo_allocator_t allocator,
                                        neo_write_context_t ctx,
                                        neo_ast_object_method_t self) {
  if (self->computed) {
    self->name->write(allocator, ctx, self->name);
  } else {
    if (self->name->type == NEO_NODE_TYPE_IDENTIFIER) {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
      char *name = neo_location_get(allocator, self->name->location);
      neo_js_program_add_string(allocator, ctx->program, name);
      neo_allocator_free(allocator, name);
    } else {
      self->name->write(allocator, ctx, self->name);
    }
  }
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
      argument->write(allocator, ctx, argument);
    }
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  }
  bool is_async = ctx->is_async;
  ctx->is_async = self->async;
  bool is_generator = ctx->is_generator;
  ctx->is_generator = self->generator;
  self->body->write(allocator, ctx, self->body);
  ctx->is_async = is_async;
  ctx->is_generator = is_generator;
  neo_writer_pop_scope(allocator, ctx, self->node.scope);
  neo_js_program_set_current(ctx->program, endaddr);
  if (self->generator) {
    if (self->async) {
      neo_js_program_add_code(allocator, ctx->program,
                              NEO_ASM_PUSH_ASYNC_GENERATOR);
    } else {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_GENERATOR);
    }
  } else {
    if (self->async) {
      neo_js_program_add_code(allocator, ctx->program,
                              NEO_ASM_PUSH_ASYNC_FUNCTION);
    } else {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_FUNCTION);
    }
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
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SET_METHOD);
}
static neo_any_t neo_serialize_ast_object_method(neo_allocator_t allocator,
                                                 neo_ast_object_method_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_OBJECT_METHOD"));
  neo_any_set(variable, "arguments",
              neo_ast_node_list_serialize(allocator, node->arguments));
  neo_any_set(variable, "name", neo_ast_node_serialize(allocator, node->name));
  neo_any_set(variable, "body", neo_ast_node_serialize(allocator, node->body));
  neo_any_set(variable, "computed",
              neo_create_any_boolean(allocator, node->computed));
  neo_any_set(variable, "async",
              neo_create_any_boolean(allocator, node->async));
  neo_any_set(variable, "generator",
              neo_create_any_boolean(allocator, node->generator));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "closure",
              neo_ast_node_list_serialize(allocator, node->closure));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_object_method_t
neo_create_ast_object_method(neo_allocator_t allocator) {
  neo_ast_object_method_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_object_method_t),
                          neo_ast_object_method_dispose);
  node->name = NULL;
  node->body = NULL;
  neo_list_initialize_t initialize = {true};
  node->arguments = neo_create_list(allocator, &initialize);
  node->node.type = NEO_NODE_TYPE_OBJECT_METHOD;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_object_method;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_object_method_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_object_method_write;
  node->computed = false;
  node->async = false;
  node->generator = false;
  node->closure = neo_create_list(allocator, NULL);
  return node;
}

neo_ast_node_t neo_ast_read_object_method(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_object_method_t node = neo_create_ast_object_method(allocator);
  neo_ast_node_t error = NULL;
  neo_token_t token = NULL;
  neo_compile_scope_t scope = NULL;
  neo_position_t cur = current;
  token = neo_read_identify_token(allocator, file, &cur);
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (token && neo_location_is(token->location, "async")) {
    error = neo_skip_all(allocator, file, &cur);
    if (error) {
      goto onerror;
    }
    if (*cur.offset != '(') {
      node->async = true;
      current = cur;
    }
  }
  neo_allocator_free(allocator, token);
  if (*current.offset == '*') {
    node->generator = true;
    current.offset++;
    current.column++;

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
  }
  node->name = neo_ast_read_object_key(allocator, file, &current);
  if (!node->name) {
    node->name = neo_ast_read_object_computed_key(allocator, file, &current);
    node->computed = true;
  }
  if (!node->name) {
    goto onerror;
  }
  NEO_CHECK_NODE(node->name, error, onerror);
  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset != '(') {
    goto onerror;
  }
  current.offset++;
  current.column++;
  scope = neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_FUNCTION,
                                 node->generator, node->async);

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset != ')') {
    for (;;) {
      neo_ast_node_t argument =
          neo_ast_read_function_argument(allocator, file, &current);
      if (!argument) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
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
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
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
  current.offset++;
  current.column++;

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  node->body = neo_ast_read_function_body(allocator, file, &current);
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