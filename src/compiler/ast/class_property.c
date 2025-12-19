#include "compiler/ast/class_property.h"
#include "compiler/asm.h"
#include "compiler/ast/decorator.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/node.h"
#include "compiler/ast/object_key.h"
#include "compiler/ast/private_name.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
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
  neo_ast_node_t error = NULL;
  if (!self->static_) {
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_THIS);
  }
  if (self->computed) {
    self->identifier->write(allocator, ctx, self->identifier);
  } else {
    if (self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
      char *name = neo_location_get(allocator, self->identifier->location);
      neo_js_program_add_string(allocator, ctx->program, name);
      neo_allocator_free(allocator, name);
    } else if (self->identifier->type != NEO_NODE_TYPE_PRIVATE_NAME) {
      self->identifier->write(allocator, ctx, self->identifier);
    }
  }
  if (self->value) {
    self->value->write(allocator, ctx, self->value);
  } else {
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_UNDEFINED);
  }
  if (self->accessor) {
    if (self->identifier->type == NEO_NODE_TYPE_PRIVATE_NAME) {
      neo_js_program_add_code(allocator, ctx->program,
                              NEO_ASM_INIT_PRIVATE_ACCESSOR);
      char *name = neo_location_get(allocator, self->identifier->location);
      neo_js_program_add_string(allocator, ctx->program, name);
      neo_allocator_free(allocator, name);
    } else {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_INIT_ACCESSOR);
    }
  } else {
    if (self->identifier->type == NEO_NODE_TYPE_PRIVATE_NAME) {
      neo_js_program_add_code(allocator, ctx->program,
                              NEO_ASM_INIT_PRIVATE_FIELD);
      char *name = neo_location_get(allocator, self->identifier->location);
      neo_js_program_add_string(allocator, ctx->program, name);
      neo_allocator_free(allocator, name);
    } else {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_INIT_FIELD);
    }
  }
  if (!self->static_) {
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  }
}

static neo_any_t
neo_serialize_ast_class_property(neo_allocator_t allocator,
                                 neo_ast_class_property_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_CLASS_PROPERTY"));
  neo_any_set(variable, "identifier",
              neo_ast_node_serialize(allocator, node->identifier));
  neo_any_set(variable, "value",
              neo_ast_node_serialize(allocator, node->value));
  neo_any_set(variable, "decorators",
              neo_ast_node_list_serialize(allocator, node->decorators));
  neo_any_set(variable, "computed",
              neo_create_any_boolean(allocator, node->computed));
  neo_any_set(variable, "accessor",
              neo_create_any_boolean(allocator, node->accessor));
  neo_any_set(variable, "static",
              neo_create_any_boolean(allocator, node->static_));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_class_property_t
neo_create_ast_class_property(neo_allocator_t allocator) {
  neo_ast_class_property_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_class_property_t),
                          neo_ast_class_property_dispose);
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
  neo_ast_node_t error = NULL;
  node = neo_create_ast_class_property(allocator);
  for (;;) {
    neo_ast_node_t decorator =
        neo_ast_read_decorator(allocator, file, &current);
    if (!decorator) {
      break;
    }
    NEO_CHECK_NODE(decorator, error, onerror);
    neo_list_push(node->decorators, decorator);

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
  }
  token = neo_read_identify_token(allocator, file, &current);
  if (token && neo_location_is(token->location, "static")) {

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
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

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  token = neo_read_identify_token(allocator, file, &current);
  if (token && neo_location_is(token->location, "accessor")) {

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
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

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  node->identifier = neo_ast_read_object_key(allocator, file, &current);
  if (!node->identifier) {
    node->identifier = neo_ast_read_private_name(allocator, file, &current);
  }
  if (!node->identifier) {
    node->identifier =
        neo_ast_read_object_computed_key(allocator, file, &current);
    node->computed = true;
  }
  if (!node->identifier) {
    goto onerror;
  }
  NEO_CHECK_NODE(node->identifier, error, onerror);
  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  token = neo_read_symbol_token(allocator, file, &current);
  if (token && neo_location_is(token->location, "=")) {

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    node->value = neo_ast_read_expression_2(allocator, file, &current);
    if (!node->value) {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
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
  return error;
}