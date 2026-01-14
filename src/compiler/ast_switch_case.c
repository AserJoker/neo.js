#include "neo.js/compiler/ast_switch_case.h"
#include "neo.js/compiler/ast_expression.h"
#include "neo.js/compiler/ast_node.h"
#include "neo.js/compiler/ast_statement.h"
#include "neo.js/compiler/token.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/any.h"
#include "neo.js/core/list.h"
#include "neo.js/core/location.h"
#include "neo.js/core/position.h"

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
static neo_any_t neo_serialize_ast_switch_case(neo_allocator_t allocator,
                                               neo_ast_switch_case_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_SWITCH_CASE"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "condition",
              neo_ast_node_serialize(allocator, node->condition));
  neo_any_set(variable, "body",
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
  node->condition = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_switch_case(neo_allocator_t allocator,
                                        const char *file,
                                        neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_node_t error = NULL;
  neo_ast_switch_case_t node = neo_create_ast_switch_case(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (token && neo_location_is(token->location, "case")) {

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    node->condition = neo_ast_read_expression(allocator, file, &current);
    if (!node->condition) {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
      goto onerror;
    }
  } else if (!token || !neo_location_is(token->location, "default")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset != ':') {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  for (;;) {
    if (*current.offset == '}') {
      break;
    }
    neo_position_t cur = current;
    token = neo_read_identify_token(allocator, file, &cur);
    if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
      error = neo_create_error_node(allocator, NULL);
      error->error = token->error;
      token->error = NULL;
      goto onerror;
    }
    if (token && (neo_location_is(token->location, "case") ||
                  neo_location_is(token->location, "default"))) {
      neo_allocator_free(allocator, token);
      break;
    }
    neo_allocator_free(allocator, token);
    neo_ast_node_t statement =
        neo_ast_read_statement(allocator, file, &current);
    if (!statement) {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
      goto onerror;
    }
    NEO_CHECK_NODE(statement, error, onerror);
    neo_list_push(node->body, statement);

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    if (*current.offset == ';') {
      current.offset++;
      current.column++;

      error = neo_skip_all(allocator, file, &current);
      if (error) {
        goto onerror;
      }
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
  return error;
}