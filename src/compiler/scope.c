#include "compiler/scope.h"
#include "compiler/ast/expression_class.h"
#include "compiler/ast/expression_function.h"
#include "compiler/ast/function_argument.h"
#include "compiler/ast/node.h"
#include "compiler/ast/pattern_array_item.h"
#include "compiler/ast/pattern_object.h"
#include "compiler/ast/pattern_object_item.h"
#include "compiler/ast/pattern_rest.h"
#include "compiler/ast/variable_declarator.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/variable.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

neo_compile_scope_t current = NULL;

static void neo_compile_scope_dispose(neo_allocator_t allocator,
                                      neo_compile_scope_t scope) {
  neo_allocator_free(allocator, scope->variables);
}

static neo_compile_scope_t neo_create_compile_scope(neo_allocator_t allocator) {
  neo_compile_scope_t scope = (neo_compile_scope_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_compile_scope_t),
      neo_compile_scope_dispose);
  scope->parent = NULL;
  neo_list_initialize_t initialize = {true};
  scope->variables = neo_create_list(allocator, &initialize);
  scope->type = NEO_COMPILE_SCOPE_FUNCTION;
  return scope;
}

neo_compile_scope_t neo_compile_scope_push(neo_allocator_t allocator,
                                           neo_compile_scope_type_t type) {
  neo_compile_scope_t scope = neo_create_compile_scope(allocator);
  scope->parent = current;
  scope->type = type;
  neo_compile_scope_t curr = current;
  current = scope;
  return curr;
}

neo_compile_scope_t neo_compile_scope_pop(neo_compile_scope_t scope) {
  neo_compile_scope_t curr = current;
  current = scope;
  return curr;
}

static void neo_compile_variable_dispose(neo_allocator_t allocator,
                                         neo_compile_variable_t variable) {}

static neo_compile_variable_t
neo_create_compile_variable(neo_allocator_t allocator) {
  neo_compile_variable_t variable = (neo_compile_variable_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_compile_variable_t),
      neo_compile_variable_dispose);
  variable->node = NULL;
  variable->type = NEO_COMPILE_VARIABLE_VAR;
  return variable;
}

void neo_compile_scope_declar_value(neo_allocator_t allocator,
                                    neo_compile_scope_t self,
                                    neo_ast_node_t node,
                                    neo_compile_variable_type_t type) {
  neo_compile_variable_t variable = neo_create_compile_variable(allocator);
  variable->type = type;
  variable->node = node;
  if (type == NEO_COMPILE_VARIABLE_VAR ||
      type == NEO_COMPILE_VARIABLE_FUNCTION) {
    while (self->type == NEO_COMPILE_SCOPE_BLOCK) {
      self = self->parent;
    }
  }
  neo_list_push(self->variables, variable);
}
void neo_compile_scope_declar(neo_allocator_t allocator,
                              neo_compile_scope_t self, neo_ast_node_t node,
                              neo_compile_variable_type_t type) {
  if (node->type == NEO_NODE_TYPE_IDENTIFIER) {
    neo_compile_scope_declar_value(allocator, self, node, type);
  } else if (node->type == NEO_NODE_TYPE_FUNCTION_ARGUMENT) {
    neo_ast_function_argument_t argument = (neo_ast_function_argument_t)node;
    neo_compile_scope_declar(allocator, self, argument->identifier,
                             NEO_COMPILE_VARIABLE_LET);
  } else if (node->type == NEO_NODE_TYPE_EXPRESSION_FUNCTION) {
    neo_ast_expression_function_t function =
        (neo_ast_expression_function_t)node;
    if (function->name) {
      neo_compile_scope_declar(allocator, self, function->name,
                               NEO_COMPILE_VARIABLE_FUNCTION);
    }
  } else if (node->type == NEO_NODE_TYPE_EXPRESSION_CLASS) {
    neo_ast_expression_class_t clazz = (neo_ast_expression_class_t)node;
    if (clazz->name) {
      neo_compile_scope_declar(allocator, self, clazz->name, type);
    }
  } else if (node->type == NEO_NODE_TYPE_EXPRESSION_CLASS) {
    neo_ast_expression_class_t clazz = (neo_ast_expression_class_t)node;
    if (clazz->name) {
      neo_compile_scope_declar(allocator, self, clazz->name, type);
    }
  } else if (node->type == NEO_NODE_TYPE_PATTERN_REST) {
    neo_ast_pattern_rest_t rest = (neo_ast_pattern_rest_t)node;
    neo_compile_scope_declar(allocator, self, rest->identifier, type);
  } else if (node->type == NEO_NODE_TYPE_PATTERN_ARRAY_ITEM) {
    neo_ast_pattern_array_item_t array = (neo_ast_pattern_array_item_t)node;
    neo_compile_scope_declar(allocator, self, array->identifier, type);
  } else if (node->type == NEO_NODE_TYPE_PATTERN_OBJECT_ITEM) {
    neo_ast_pattern_object_item_t object = (neo_ast_pattern_object_item_t)node;
    if (object->alias) {
      neo_compile_scope_declar(allocator, self, object->alias, type);
    } else {
      neo_compile_scope_declar(allocator, self, object->identifier, type);
    }
  } else if (node->type == NEO_NODE_TYPE_VARIABLE_DECLARATOR) {
    neo_ast_variable_declarator_t variable =
        (neo_ast_variable_declarator_t)node;
    neo_compile_scope_declar(allocator, self, variable->identifier, type);
  } else if (node->type == NEO_NODE_TYPE_FUNCTION_ARGUMENT) {
    neo_ast_function_argument_t argument = (neo_ast_function_argument_t)node;
    neo_compile_scope_declar(allocator, self, argument->identifier, type);
  } else if (node->type == NEO_NODE_TYPE_PATTERN_OBJECT) {
    neo_ast_pattern_object_t object = (neo_ast_pattern_object_t)node;
    for (neo_list_node_t it = neo_list_get_first(object->items);
         it != neo_list_get_tail(object->items); it = neo_list_node_next(it)) {
      neo_compile_scope_declar(allocator, self, neo_list_node_get(it), type);
    }
  } else if (node->type == NEO_NODE_TYPE_PATTERN_ARRAY) {
    neo_ast_pattern_object_t object = (neo_ast_pattern_object_t)node;
    for (neo_list_node_t it = neo_list_get_first(object->items);
         it != neo_list_get_tail(object->items); it = neo_list_node_next(it)) {
      neo_compile_scope_declar(allocator, self, neo_list_node_get(it), type);
    }
  } else {
    THROW("SyntaxError",
          "Illegal property in declaration context\n  at %s:%d:%d",
          node->location.file, node->location.begin.line,
          node->location.begin.column);
    return;
  }
}
neo_compile_scope_t neo_compile_scope_get_current() { return current; }

static neo_variable_t
neo_serialize_compile_variable(neo_allocator_t allocator,
                               neo_compile_variable_t val) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "name",
                   neo_ast_node_serialize(allocator, val->node));
  switch (val->type) {
  case NEO_COMPILE_VARIABLE_VAR:
    neo_variable_set(
        variable, "type",
        neo_create_variable_string(allocator, "NEO_COMPILE_VARIABLE_VAR"));
    break;
  case NEO_COMPILE_VARIABLE_LET:
    neo_variable_set(
        variable, "type",
        neo_create_variable_string(allocator, "NEO_COMPILE_VARIABLE_LET"));
    break;
  case NEO_COMPILE_VARIABLE_CONST:
    neo_variable_set(
        variable, "type",
        neo_create_variable_string(allocator, "NEO_COMPILE_VARIABLE_CONST"));
    break;
  case NEO_COMPILE_VARIABLE_FUNCTION:
    neo_variable_set(
        variable, "type",
        neo_create_variable_string(allocator, "NEO_COMPILE_VARIABLE_FUNCTION"));
    break;
  }
  return variable;
}

neo_variable_t neo_serialize_scope(neo_allocator_t allocator,
                                   neo_compile_scope_t scope) {
  if (!scope) {
    return neo_create_variable_nil(allocator);
  }
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  switch (scope->type) {
  case NEO_COMPILE_SCOPE_BLOCK:
    neo_variable_set(
        variable, "type",
        neo_create_variable_string(allocator, "NEO_COMPILE_SCOPE_BLOCK"));
    break;
  case NEO_COMPILE_SCOPE_FUNCTION:
    neo_variable_set(
        variable, "type",
        neo_create_variable_string(allocator, "NEO_COMPILE_SCOPE_FUNCTION"));
    break;
  }
  neo_variable_set(variable, "variables",
                   neo_create_variable_array(
                       allocator, scope->variables,
                       (neo_serialize_fn_t)neo_serialize_compile_variable));
  return variable;
}
