#include "compiler/switch_case.h"
#include "compiler/expression.h"
#include "compiler/node.h"
#include "compiler/statement.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_switch_case_dispose(neo_allocator_t allocator,
                                        neo_ast_switch_case_t node) {
  neo_allocator_free(allocator, node->condition);
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->node.scope);
}
static neo_variable_t
neo_serialize_ast_switch_case(neo_allocator_t allocator,
                              neo_ast_switch_case_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_SWITCH_CASE"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "condition",
                   neo_ast_node_serialize(allocator, node->condition));
  neo_variable_set(variable, "body",
                   neo_ast_node_list_serialize(allocator, node->body));
  return variable;
}
static neo_ast_switch_case_t
neo_create_ast_switch_case(neo_allocator_t allocator) {
  neo_ast_switch_case_t node =
      neo_allocator_alloc2(allocator, neo_ast_switch_case);
  node->node.type = NEO_NODE_TYPE_SWITCH_CASE;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_switch_case;
  neo_list_initialize_t initialize = {true};
  node->body = neo_create_list(allocator, &initialize);
  return node;
}

neo_ast_node_t neo_ast_read_switch_case(neo_allocator_t allocator,
                                        const char *file,
                                        neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_switch_case_t node = neo_create_ast_switch_case(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (token && neo_location_is(token->location, "case")) {
    SKIP_ALL(allocator, file, &current, onerror);
    node->condition = TRY(neo_ast_read_expression(allocator, file, &current)) {
      goto onerror;
    }
    if (!node->condition) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
  } else if (!token || !neo_location_is(token->location, "default")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ':') {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  for (;;) {
    if (*current.offset == '}') {
      break;
    }
    neo_position_t cur = current;
    token = neo_read_identify_token(allocator, file, &cur);
    if (token && (neo_location_is(token->location, "case") ||
                  neo_location_is(token->location, "default"))) {
      neo_allocator_free(allocator, token);
      break;
    }
    neo_allocator_free(allocator, token);
    neo_ast_node_t statement =
        TRY(neo_ast_read_statement(allocator, file, &current)) {
      goto onerror;
    }
    if (!statement) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
    neo_list_push(node->body, statement);
    SKIP_ALL(allocator, file, &current, onerror);
    if (*current.offset == ';') {
      current.offset++;
      current.column++;
      SKIP_ALL(allocator, file, &current, onerror);
    }
  }
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