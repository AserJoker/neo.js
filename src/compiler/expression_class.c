#include "compiler/expression_class.h"
#include "compiler/class_accessor.h"
#include "compiler/class_method.h"
#include "compiler/class_property.h"
#include "compiler/declaration_class.h"
#include "compiler/declaration_export.h"
#include "compiler/decorator.h"
#include "compiler/expression.h"
#include "compiler/identifier.h"
#include "compiler/node.h"
#include "compiler/static_block.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_expression_class_dispose(neo_allocator_t allocator,
                                             neo_ast_expression_class_t node) {
  neo_allocator_free(allocator, node->name);
  neo_allocator_free(allocator, node->extends);
  neo_allocator_free(allocator, node->items);
  neo_allocator_free(allocator, node->decorators);
  neo_allocator_free(allocator, node->node.scope);
}

static neo_variable_t
neo_serialize_ast_expression_class(neo_allocator_t allocator,
                                   neo_ast_expression_class_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_EXPRESSION_CALL"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "name",
                   neo_ast_node_serialize(allocator, node->name));
  neo_variable_set(variable, "extends",
                   neo_ast_node_serialize(allocator, node->extends));
  neo_variable_set(variable, "items",
                   neo_ast_node_list_serialize(allocator, node->items));
  neo_variable_set(variable, "decorators",
                   neo_ast_node_list_serialize(allocator, node->decorators));
  return variable;
}

static neo_ast_expression_class_t
neo_create_ast_expression_class(neo_allocator_t allocator) {
  neo_ast_expression_class_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_class);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_CLASS;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_expression_class;
  node->name = NULL;
  node->extends = NULL;
  neo_list_initialize_t initialize = {true};
  node->items = neo_create_list(allocator, &initialize);
  node->decorators = neo_create_list(allocator, &initialize);
  return node;
}

neo_ast_node_t neo_ast_read_expression_class(neo_allocator_t allocator,
                                             const char *file,
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
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
    if (neo_list_get_size(export->specifiers) != 1) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      neo_allocator_free(allocator, export);
      goto onerror;
    }
    neo_ast_declaration_class_t dclazz =
        (neo_ast_declaration_class_t)neo_list_node_get(
            neo_list_get_first(export->specifiers));
    if (dclazz->node.type != NEO_NODE_TYPE_DECLARATION_CLASS) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
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
  node->name = TRY(neo_ast_read_identifier(allocator, file, &current)) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  token = neo_read_identify_token(allocator, file, &current);
  SKIP_ALL(allocator, file, &current, onerror);
  if (token && neo_location_is(token->location, "extends")) {
    SKIP_ALL(allocator, file, &current, onerror);
    if (*current.offset == '{') {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
    node->extends = neo_ast_read_expression_2(allocator, file, &current);
    SKIP_ALL(allocator, file, &current, onerror);
  }
  neo_allocator_free(allocator, token);
  if (*current.offset != '{') {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
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
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
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
      } else if (current.line == line &
                 item->type == NEO_NODE_TYPE_CLASS_PROPERTY) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
    }
  }
  current.offset++;
  current.column++;
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