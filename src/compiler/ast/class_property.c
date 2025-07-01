#include "compiler/ast/class_property.h"
#include "compiler/asm.h"
#include "compiler/ast/decorator.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/node.h"
#include "compiler/ast/object_key.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>
static void neo_ast_class_property_dispose(neo_allocator_t allocator,
                                           neo_ast_class_property_t node) {
  neo_allocator_free(allocator, node->decorators);
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->value);
  neo_allocator_free(allocator, node->node.scope);
}

static void neo_ast_class_method_resolve_closure(neo_allocator_t allocator,
                                                 neo_ast_class_property_t self,
                                                 neo_list_t closure) {
  if (self->computed) {
    self->identifier->resolve_closure(allocator, self->identifier, closure);
  }
  for (neo_list_node_t it = neo_list_get_first(self->decorators);
       it != neo_list_get_tail(self->decorators); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
  if (self->value) {
    self->value->resolve_closure(allocator, self->value, closure);
  }
}

static void neo_ast_class_property_write(neo_allocator_t allocator,
                                         neo_write_context_t ctx,
                                         neo_ast_class_property_t self) {
  if (!self->static_) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_THIS);
  }
  if (!self->computed && self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
    char *name = neo_location_get(allocator, self->identifier->location);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  } else {
    TRY(self->identifier->write(allocator, ctx, self->identifier)) { return; }
  }
  if (self->value) {
    TRY(self->value->write(allocator, ctx, self->value)) { return; }
  } else {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_UNDEFINED);
  }
  if (self->accessor) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_INIT_ACCESSOR);
  } else {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_INIT_FIELD);
  }
  if (!self->static_) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  }
}

static neo_variable_t
neo_serialize_ast_class_property(neo_allocator_t allocator,
                                 neo_ast_class_property_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_CLASS_PROPERTY"));
  neo_variable_set(variable, "identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  neo_variable_set(variable, "value",
                   neo_ast_node_serialize(allocator, node->value));
  neo_variable_set(variable, "decorators",
                   neo_ast_node_list_serialize(allocator, node->decorators));
  neo_variable_set(variable, "computed",
                   neo_create_variable_boolean(allocator, node->computed));
  neo_variable_set(variable, "accessor",
                   neo_create_variable_boolean(allocator, node->accessor));
  neo_variable_set(variable, "static",
                   neo_create_variable_boolean(allocator, node->static_));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_class_property_t
neo_create_ast_class_property(neo_allocator_t allocator) {
  neo_ast_class_property_t node =
      neo_allocator_alloc2(allocator, neo_ast_class_property);
  node->node.type = NEO_NODE_TYPE_CLASS_PROPERTY;
  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_class_property;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_class_method_resolve_closure;
  node->computed = false;
  node->static_ = false;
  node->accessor = false;
  node->value = NULL;
  node->identifier = NULL;
  neo_list_initialize_t initialize = {true};
  node->decorators = neo_create_list(allocator, &initialize);
  node->node.write = (neo_write_fn_t)neo_ast_class_property_write;
  return node;
}

neo_ast_node_t neo_ast_read_class_property(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_class_property_t node = NULL;
  node = neo_create_ast_class_property(allocator);
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
    SKIP_ALL(allocator, file, &current, onerror);
    if (current.line == token->location.begin.line) {
      neo_token_t next = neo_read_identify_token(allocator, file, &current);
      if (next) {
        node->static_ = true;
        current = next->location.begin;
      } else {
        current = token->location.begin;
      }
      neo_allocator_free(allocator, next);
    } else {
      current = token->location.begin;
    }
  } else if (token) {
    current = token->location.begin;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  token = neo_read_identify_token(allocator, file, &current);
  if (token && neo_location_is(token->location, "accessor")) {
    SKIP_ALL(allocator, file, &current, onerror);
    if (current.line == token->location.begin.line) {
      neo_token_t next = neo_read_identify_token(allocator, file, &current);
      if (next) {
        node->accessor = true;
        current = next->location.begin;
      } else {
        current = token->location.begin;
      }
      neo_allocator_free(allocator, next);
    } else {
      current = token->location.begin;
    }
  } else if (token) {
    current = token->location.begin;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->identifier = TRY(neo_ast_read_object_key(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_object_computed_key(allocator, file, &current)) {
      goto onerror;
    }
    node->computed = true;
  }
  if (!node->identifier) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  token = neo_read_symbol_token(allocator, file, &current);
  if (token && neo_location_is(token->location, "=")) {
    SKIP_ALL(allocator, file, &current, onerror);
    node->value = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
      goto onerror;
    }
    if (!node->value) {
      THROW("Invalid or unexpected token \n  at %s:%d:%d", file, current.line,
            current.column);
      goto onerror;
    }
  } else {
    current = node->identifier->location.end;
  }
  neo_allocator_free(allocator, token);
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  neo_allocator_free(allocator, token);
  return NULL;
}