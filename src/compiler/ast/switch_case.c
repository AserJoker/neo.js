#include "compiler/ast/switch_case.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/node.h"
#include "compiler/ast/statement.h"
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
static void neo_ast_switch_case_resolve_closure(neo_allocator_t allocator,
                                                neo_ast_switch_case_t self,
                                                neo_list_t closure) {
  if (self->condition) {
    self->condition->resolve_closure(allocator, self->condition, closure);
  }
  for (neo_list_node_t it = neo_list_get_first(self->body);
       it != neo_list_get_tail(self->body); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
}
static neo_variable_t
neo_serialize_ast_switch_case(neo_allocator_t allocator,
                              neo_ast_switch_case_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, L"type",
      neo_create_variable_string(allocator, L"NEO_NODE_TYPE_SWITCH_CASE"));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, L"condition",
                   neo_ast_node_serialize(allocator, node->condition));
  neo_variable_set(variable, L"body",
                   neo_ast_node_list_serialize(allocator, node->body));
  return variable;
}
static neo_ast_switch_case_t
neo_create_ast_switch_case(neo_allocator_t allocator) {
  neo_ast_switch_case_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_switch_case_t),
                          neo_ast_switch_case_dispose);
  node->node.type = NEO_NODE_TYPE_SWITCH_CASE;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_switch_case;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_switch_case_resolve_closure;
  neo_list_initialize_t initialize = {true};
  node->body = neo_create_list(allocator, &initialize);
  node->node.write = NULL;
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
      THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
      goto onerror;
    }
  } else if (!token || !neo_location_is(token->location, "default")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ':') {
    THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
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
      THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
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