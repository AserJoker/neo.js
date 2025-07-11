#include "compiler/ast/expression_class.h"
#include "compiler/asm.h"
#include "compiler/ast/class_accessor.h"
#include "compiler/ast/class_method.h"
#include "compiler/ast/class_property.h"
#include "compiler/ast/declaration_class.h"
#include "compiler/ast/declaration_export.h"
#include "compiler/ast/decorator.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/node.h"
#include "compiler/ast/static_block.h"
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
#include <wchar.h>

static void neo_ast_expression_class_dispose(neo_allocator_t allocator,
                                             neo_ast_expression_class_t node) {
  neo_allocator_free(allocator, node->name);
  neo_allocator_free(allocator, node->extends);
  neo_allocator_free(allocator, node->items);
  neo_allocator_free(allocator, node->decorators);
  neo_allocator_free(allocator, node->node.scope);
  neo_allocator_free(allocator, node->closure);
}

static void
neo_ast_expression_class_resolve_closure(neo_allocator_t allocator,
                                         neo_ast_expression_class_t self,
                                         neo_list_t closure) {
  for (neo_list_node_t it = neo_list_get_first(self->decorators);
       it != neo_list_get_tail(self->decorators); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
  neo_compile_scope_t current = neo_compile_scope_set(self->node.scope);
  if (self->extends) {
    self->extends->resolve_closure(allocator, self->extends, closure);
  }
  neo_compile_scope_set(current);
  for (neo_list_node_t it = neo_list_get_first(self->closure);
       it != neo_list_get_tail(self->closure); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
}

static void neo_ast_expression_class_write(neo_allocator_t allocator,
                                           neo_write_context_t ctx,
                                           neo_ast_expression_class_t self) {
  neo_ast_class_method_t constructor = NULL;

  neo_program_add_code(allocator, ctx->program, NEO_ASM_JMP);
  size_t endaddr = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  size_t begin = neo_buffer_get_size(ctx->program->codes);
  neo_writer_push_scope(allocator, ctx, self->node.scope);
  for (neo_list_node_t it = neo_list_get_first(self->items);
       it != neo_list_get_tail(self->items); it = neo_list_node_next(it)) {
    neo_ast_node_t node = neo_list_node_get(it);
    if (node->type == NEO_NODE_TYPE_CLASS_METHOD &&
        neo_location_is(node->location, "constructor")) {
      constructor = (neo_ast_class_method_t)node;
    }
    if (node->type == NEO_NODE_TYPE_CLASS_PROPERTY) {
      neo_ast_class_property_t property = (neo_ast_class_property_t)node;
      if (!property->static_) {
        TRY(node->write(allocator, ctx, node)) { return; }
      }
    }
    if (node->type == NEO_NODE_TYPE_CLASS_ACCESSOR) {
      neo_ast_class_accessor_t accessor = (neo_ast_class_accessor_t)node;
      if (!accessor->static_) {
        TRY(node->write(allocator, ctx, node)) { return; }
      }
    }
  }
  if (constructor) {
    if (neo_list_get_size(constructor->arguments)) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_LOAD);
      neo_program_add_string(allocator, ctx->program, L"arguments");
      neo_program_add_code(allocator, ctx->program, NEO_ASM_ITERATOR);
      for (neo_list_node_t it = neo_list_get_first(constructor->arguments);
           it != neo_list_get_tail(constructor->arguments);
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
    TRY(constructor->body->write(allocator, ctx, constructor->body)) { return; }
    ctx->is_generator = is_generator;
    ctx->is_async = is_async;
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_UNDEFINED);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_RET);
  neo_writer_pop_scope(allocator, ctx, self->node.scope);
  neo_program_set_current(ctx->program, endaddr);

  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_FUNCTION);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_ADDRESS);
  neo_program_add_address(allocator, ctx->program, begin);
  wchar_t *source = neo_location_get(allocator, self->node.location);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_SOURCE);
  neo_program_add_string(allocator, ctx->program, source);
  neo_allocator_free(allocator, source);
  for (neo_list_node_t it = neo_list_get_first(self->closure);
       it != neo_list_get_tail(self->closure); it = neo_list_node_next(it)) {
    neo_ast_node_t node = neo_list_node_get(it);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_CLOSURE);
    wchar_t *name = neo_location_get(allocator, node->location);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  }
  if (self->name) {
    wchar_t *name = neo_location_get(allocator, self->name->location);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_NAME);
    neo_program_add_string(allocator, ctx->program, name);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
    neo_program_add_string(allocator, ctx->program, name);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_LOAD);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  }
  if (self->extends) {
    TRY(self->extends->write(allocator, ctx, self->extends)) { return; }
    neo_program_add_code(allocator, ctx->program, NEO_ASM_EXTENDS);
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_JMP);
  size_t static_init_end = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  size_t static_init_begin = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_THIS);
  for (neo_list_node_t it = neo_list_get_first(self->items);
       it != neo_list_get_tail(self->items); it = neo_list_node_next(it)) {
    neo_ast_node_t node = neo_list_node_get(it);
    if (node->type == NEO_NODE_TYPE_STATIC_BLOCK) {
      neo_writer_push_scope(allocator, ctx, node->scope);
      TRY(node->write(allocator, ctx, node)) { return; }
      neo_writer_pop_scope(allocator, ctx, node->scope);
    }
    if (node->type == NEO_NODE_TYPE_CLASS_METHOD) {
      TRY(node->write(allocator, ctx, node)) { return; }
    }
    if (node->type == NEO_NODE_TYPE_CLASS_PROPERTY) {
      neo_ast_class_property_t property = (neo_ast_class_property_t)node;
      if (property->static_) {
        TRY(node->write(allocator, ctx, node)) { return; }
      }
    }
    if (node->type == NEO_NODE_TYPE_CLASS_ACCESSOR) {
      neo_ast_class_accessor_t accessor = (neo_ast_class_accessor_t)node;
      if (accessor->static_) {
        TRY(node->write(allocator, ctx, node)) { return; }
      }
    }
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_UNDEFINED);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_RET);
  neo_program_set_current(ctx->program, static_init_end);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_FUNCTION);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_ADDRESS);
  neo_program_add_address(allocator, ctx->program, static_init_begin);
  for (neo_list_node_t it = neo_list_get_first(self->closure);
       it != neo_list_get_tail(self->closure); it = neo_list_node_next(it)) {
    neo_ast_node_t node = neo_list_node_get(it);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_CLOSURE);
    wchar_t *name = neo_location_get(allocator, node->location);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
  neo_program_add_string(allocator, ctx->program, L"call");
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_ARRAY);
  neo_program_add_number(allocator, ctx->program, 0);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_NUMBER);
  neo_program_add_number(allocator, ctx->program, 0);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
  neo_program_add_integer(allocator, ctx->program, 5);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_MEMBER_CALL);
  neo_program_add_integer(allocator, ctx->program,
                          self->node.location.begin.line);
  neo_program_add_integer(allocator, ctx->program,
                          self->node.location.begin.column);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
}

static neo_variable_t
neo_serialize_ast_expression_class(neo_allocator_t allocator,
                                   neo_ast_expression_class_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, L"type",
      neo_create_variable_string(allocator, L"NEO_NODE_TYPE_EXPRESSION_CALL"));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, L"name",
                   neo_ast_node_serialize(allocator, node->name));
  neo_variable_set(variable, L"extends",
                   neo_ast_node_serialize(allocator, node->extends));
  neo_variable_set(variable, L"items",
                   neo_ast_node_list_serialize(allocator, node->items));
  neo_variable_set(variable, L"decorators",
                   neo_ast_node_list_serialize(allocator, node->decorators));
  neo_variable_set(variable, L"closure",
                   neo_ast_node_list_serialize(allocator, node->closure));
  return variable;
}

static neo_ast_expression_class_t
neo_create_ast_expression_class(neo_allocator_t allocator) {
  neo_ast_expression_class_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_class);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_CLASS;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_expression_class;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_expression_class_resolve_closure;
  node->name = NULL;
  node->extends = NULL;
  neo_list_initialize_t initialize = {true};
  node->items = neo_create_list(allocator, &initialize);
  node->decorators = neo_create_list(allocator, &initialize);
  node->closure = neo_create_list(allocator, NULL);
  node->node.write = (neo_write_fn_t)neo_ast_expression_class_write;
  return node;
}

neo_ast_node_t neo_ast_read_expression_class(neo_allocator_t allocator,
                                             const wchar_t *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_class_t node = NULL;
  neo_token_t token = NULL;
  node = neo_create_ast_expression_class(allocator);
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
  if (token && neo_location_is(token->location, "export") &&
      neo_list_get_size(node->decorators) != 0) {
    neo_position_t cur = token->location.begin;
    neo_allocator_free(allocator, token);
    SKIP_ALL(allocator, file, &cur, onerror);
    neo_ast_declaration_export_t export = (neo_ast_declaration_export_t)TRY(
        neo_ast_read_declaration_export(allocator, file, &cur)) {
      goto onerror;
    };
    if (!export) {
      THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
            current.line, current.column);
      goto onerror;
    }
    if (neo_list_get_size(export->specifiers) != 1) {
      THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
            current.line, current.column);
      neo_allocator_free(allocator, export);
      goto onerror;
    }
    neo_ast_declaration_class_t dclazz =
        (neo_ast_declaration_class_t)neo_list_node_get(
            neo_list_get_first(export->specifiers));
    if (dclazz->node.type != NEO_NODE_TYPE_DECLARATION_CLASS) {
      THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
            current.line, current.column);
      neo_allocator_free(allocator, export);
      goto onerror;
    }
    neo_ast_expression_class_t clazz =
        (neo_ast_expression_class_t)dclazz->declaration;
    neo_allocator_free(allocator, clazz->decorators);
    clazz->decorators = node->decorators;
    node->decorators = NULL;
    neo_allocator_free(allocator, node);
    current = cur;
    export->node.location.begin = *position;
    dclazz->node.location.begin = *position;
    clazz->node.location.begin = *position;
    *position = current;
    return &export->node;
  }
  if (!token || !neo_location_is(token->location, "class")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  neo_compile_scope_t scope = neo_compile_scope_push(
      allocator, NEO_COMPILE_SCOPE_FUNCTION, false, false);
  node->name = TRY(neo_ast_read_identifier(allocator, file, &current)) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  token = neo_read_identify_token(allocator, file, &current);
  SKIP_ALL(allocator, file, &current, onerror);
  if (token && neo_location_is(token->location, "extends")) {
    SKIP_ALL(allocator, file, &current, onerror);
    if (*current.offset == '{') {
      THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
            current.line, current.column);
      goto onerror;
    }
    node->extends = neo_ast_read_expression_2(allocator, file, &current);
    SKIP_ALL(allocator, file, &current, onerror);
  }
  neo_allocator_free(allocator, token);
  if (*current.offset != '{') {
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '}') {
    for (;;) {
      neo_ast_node_t item = NULL;
      if (!item) {
        item = TRY(neo_ast_read_static_block(allocator, file, &current)) {
          goto onerror;
        }
      }
      if (!item) {
        item = TRY(neo_ast_read_class_accessor(allocator, file, &current)) {
          goto onerror;
        }
      }
      if (!item) {
        item = TRY(neo_ast_read_class_method(allocator, file, &current)) {
          goto onerror;
        }
      }
      if (!item) {
        item = TRY(neo_ast_read_class_property(allocator, file, &current)) {
          goto onerror;
        }
      }
      if (!item) {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
              current.line, current.column);
        goto onerror;
      }
      item->resolve_closure(allocator, item, node->closure);
      neo_list_push(node->items, item);
      uint32_t line = current.line;
      SKIP_ALL(allocator, file, &current, onerror);
      if (*current.offset == ';') {
        while (*current.offset == ';') {
          current.offset++;
          current.column++;
          SKIP_ALL(allocator, file, &current, onerror);
        }
      } else if (*current.offset == '}') {
        break;
      } else if (current.line == line &&
                 item->type == NEO_NODE_TYPE_CLASS_PROPERTY) {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
              current.line, current.column);
        goto onerror;
      }
    }
  }
  current.offset++;
  current.column++;
  node->node.scope = neo_compile_scope_pop(scope);
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