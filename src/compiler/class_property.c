#include "compiler/class_property.h"
#include "compiler/decorator.h"
#include "compiler/expression.h"
#include "compiler/node.h"
#include "compiler/object_key.h"
#include "compiler/token.h"
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
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_class_property;
  node->computed = false;
  node->static_ = false;
  node->value = NULL;
  node->identifier = NULL;
  neo_list_initialize_t initialize = {true};
  node->decorators = neo_create_list(allocator, &initialize);
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
    node->static_ = true;
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
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
  } else if (token) {
    current = token->location.begin;
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