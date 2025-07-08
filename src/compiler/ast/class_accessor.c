#include "compiler/ast/class_accessor.h"
#include "compiler/asm.h"
#include "compiler/ast/decorator.h"
#include "compiler/ast/function_argument.h"
#include "compiler/ast/function_body.h"
#include "compiler/ast/node.h"
#include "compiler/ast/object_accessor.h"
#include "compiler/ast/object_key.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_class_accessor_dispose(neo_allocator_t allocator,
                                           neo_ast_class_accessor_t node) {
  neo_allocator_free(allocator, node->arguments);
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->decorators);
  neo_allocator_free(allocator, node->name);
  neo_allocator_free(allocator, node->closure);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_class_accessor_resolve_closure(neo_allocator_t allocator,
                                       neo_ast_class_accessor_t self,
                                       neo_list_t closure) {
  if (self->computed) {
    self->name->resolve_closure(allocator, self->name, closure);
  }
  for (neo_list_node_t it = neo_list_get_first(self->decorators);
       it != neo_list_get_tail(self->decorators); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
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
static void neo_ast_class_accessor_write(neo_allocator_t allocator,
                                         neo_write_context_t ctx,
                                         neo_ast_class_accessor_t self) {
  if (!self->static_) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_THIS);
  }
  if (!self->computed && self->name->type == NEO_NODE_TYPE_IDENTIFIER) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
    char *name = neo_location_get(allocator, self->name->location);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  } else {
    TRY(self->name->write(allocator, ctx, self->name)) { return; }
  }
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
  bool is_async = ctx->is_async;
  bool is_generator = ctx->is_generator;
  ctx->is_async = false;
  ctx->is_generator = false;
  TRY(self->body->write(allocator, ctx, self->body)) { return; }
  ctx->is_generator = is_generator;
  ctx->is_async = is_async;
  neo_writer_pop_scope(allocator, ctx, self->node.scope);
  neo_program_set_current(ctx->program, endaddr);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_FUNCTION);
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
  if (self->kind == NEO_ACCESSOR_KIND_GET) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_GETTER);
  } else {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_SETTER);
  }
  if (!self->static_) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  }
}

static neo_variable_t
neo_serialize_ast_class_accessor(neo_allocator_t allocator,
                                 neo_ast_class_accessor_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_CLASS_ACCESSOR"));
  neo_variable_set(variable, "arguments",
                   neo_ast_node_list_serialize(allocator, node->arguments));
  neo_variable_set(variable, "name",
                   neo_ast_node_serialize(allocator, node->name));
  neo_variable_set(variable, "body",
                   neo_ast_node_serialize(allocator, node->body));
  neo_variable_set(variable, "decorators",
                   neo_ast_node_list_serialize(allocator, node->decorators));
  neo_variable_set(
      variable, "kind",
      neo_create_variable_string(allocator, node->kind == NEO_ACCESSOR_KIND_GET
                                                ? "NEO_ACCESSOR_KIND_GET"
                                                : "NEO_ACCESSOR_KIND_SET"));
  neo_variable_set(variable, "static",
                   neo_create_variable_boolean(allocator, node->static_));
  neo_variable_set(variable, "computed",
                   neo_create_variable_boolean(allocator, node->computed));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "closure",
                   neo_ast_node_list_serialize(allocator, node->closure));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_class_accessor_t
neo_create_ast_class_accessor(neo_allocator_t allocator) {
  neo_ast_class_accessor_t node =
      neo_allocator_alloc2(allocator, neo_ast_class_accessor);
  neo_list_initialize_t initialize = {true};
  node->node.type = NEO_NODE_TYPE_CLASS_ACCESSOR;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_class_accessor;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_class_accessor_resolve_closure;
  node->node.scope = NULL;
  node->arguments = neo_create_list(allocator, &initialize);
  node->body = NULL;
  node->computed = false;
  node->decorators = neo_create_list(allocator, &initialize);
  node->kind = NEO_ACCESSOR_KIND_GET;
  node->name = NULL;
  node->static_ = false;
  node->closure = neo_create_list(allocator, NULL);
  node->node.write = (neo_write_fn_t)neo_ast_class_accessor_write;
  return node;
}

neo_ast_node_t neo_ast_read_class_accessor(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_class_accessor_t node = NULL;
  neo_token_t token = NULL;
  neo_compile_scope_t scope = NULL;
  node = neo_create_ast_class_accessor(allocator);
  for (;;) {
    neo_ast_node_t decorator =
        TRY(neo_ast_read_decorator(allocator, file, &current)) {
      goto onerror;
    }
    if (!decorator) {
      break;
    }
    neo_list_push(node->decorators, decorator);
    SKIP_ALL(allocator, file, &current, onerror);
  }
  token = neo_read_identify_token(allocator, file, &current);
  if (token && neo_location_is(token->location, "static")) {
    node->static_ = true;
    SKIP_ALL(allocator, file, &current, onerror);
  } else if (token) {
    current = token->location.begin;
  }
  neo_allocator_free(allocator, token);
  token = neo_read_identify_token(allocator, file, &current);
  if (token && neo_location_is(token->location, "get")) {
    node->kind = NEO_ACCESSOR_KIND_GET;
  } else if (token && neo_location_is(token->location, "set")) {
    node->kind = NEO_ACCESSOR_KIND_SET;
  } else {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->name = TRY(neo_ast_read_object_key(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->name) {
    node->name = TRY(neo_ast_read_object_key(allocator, file, &current)) {
      goto onerror;
    }
    node->computed = true;
  }
  if (!node->name) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '(') {
    THROW("Invalid or unexpected token \n  at _.compile(%s:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  scope = neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_FUNCTION, false,
                                 false);
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ')') {
    for (;;) {
      neo_ast_node_t argument =
          TRY(neo_ast_read_function_argument(allocator, file, &current)) {
        goto onerror;
      }
      if (!argument) {
        THROW("Invalid or unexpected token \n  at _.compile(%s:%d:%d)", file,
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
        THROW("Invalid or unexpected token \n  at _.compile(%s:%d:%d)", file,
              current.line, current.column);
        goto onerror;
      }
      current.offset++;
      current.column++;
      SKIP_ALL(allocator, file, &current, onerror);
    }
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  node->body = TRY(neo_ast_read_function_body(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->body) {
    THROW("Invalid or unexpected token \n  at _.compile(%s:%d:%d)", file,
          current.line, current.column);
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