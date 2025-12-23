#include "compiler/ast_expression.h"
#include "compiler/ast_expression_member.h"
#include "compiler/ast_identifier.h"
#include "compiler/ast_node.h"
#include "compiler/ast_private_name.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/location.h"
#include "core/position.h"
#include <stdio.h>


static void
neo_ast_expression_member_dispose(neo_allocator_t allocator,
                                  neo_ast_expression_member_t node) {
  neo_allocator_free(allocator, node->field);
  neo_allocator_free(allocator, node->host);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_expression_member_resolve_closure(neo_allocator_t allocator,
                                          neo_ast_expression_member_t self,
                                          neo_list_t closure) {
  self->host->resolve_closure(allocator, self->host, closure);
  if (self->node.type == NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER ||
      self->node.type == NEO_NODE_TYPE_EXPRESSION_OPTIONAL_COMPUTED_MEMBER) {
    self->field->resolve_closure(allocator, self->field, closure);
  }
}

static void neo_ast_expression_member_write(neo_allocator_t allocator,
                                            neo_write_context_t ctx,
                                            neo_ast_expression_member_t self) {
  neo_list_initialize_t initialize = {true};
  neo_list_t addresses = neo_create_list(allocator, &initialize);
  neo_write_optional_chain(allocator, ctx, &self->node, addresses);
  for (neo_list_node_t it = neo_list_get_first(addresses);
       it != neo_list_get_tail(addresses); it = neo_list_node_next(it)) {
    size_t *address = neo_list_node_get(it);
    neo_js_program_set_current(ctx->program, *address);
  }
  neo_allocator_free(allocator, addresses);
}

static neo_any_t
neo_serialize_ast_expression_member(neo_allocator_t allocator,
                                    neo_ast_expression_member_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  if (node->node.type == NEO_NODE_TYPE_EXPRESSION_MEMBER) {
    neo_any_set(
        variable, "type",
        neo_create_any_string(allocator, "NEO_NODE_TYPE_EXPRESSION_MEMBER"));
  } else if (node->node.type == NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER) {
    neo_any_set(variable, "type",
                neo_create_any_string(
                    allocator, "NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER"));
  } else if (node->node.type == NEO_NODE_TYPE_EXPRESSION_OPTIONAL_MEMBER) {
    neo_any_set(variable, "type",
                neo_create_any_string(
                    allocator, "NEO_NODE_TYPE_EXPRESSION_OPTIONAL_MEMBER"));
  } else if (node->node.type ==
             NEO_NODE_TYPE_EXPRESSION_OPTIONAL_COMPUTED_MEMBER) {
    neo_any_set(
        variable, "type",
        neo_create_any_string(
            allocator, "NEO_NODE_TYPE_EXPRESSION_OPTIONAL_COMPUTED_MEMBER"));
  }
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "host", neo_ast_node_serialize(allocator, node->host));
  neo_any_set(variable, "field",
              neo_ast_node_serialize(allocator, node->field));
  return variable;
}

static neo_ast_expression_member_t
neo_create_ast_expression_member(neo_allocator_t allocator) {
  neo_ast_expression_member_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_expression_member_t),
      neo_ast_expression_member_dispose);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_MEMBER;

  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_expression_member;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_expression_member_resolve_closure;
  node->field = NULL;
  node->host = NULL;
  node->node.write = (neo_write_fn_t)neo_ast_expression_member_write;
  return node;
}

neo_ast_node_t neo_ast_read_expression_member(neo_allocator_t allocator,
                                              const char *file,
                                              neo_position_t *position) {
  neo_ast_node_t error = NULL;
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_expression_member_t node =
      neo_create_ast_expression_member(allocator);
  token = neo_read_symbol_token(allocator, file, &current);
  if (!token) {
    goto onerror;
  }

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (neo_location_is(token->location, ".")) {
    neo_allocator_free(allocator, token);
    node->field = neo_ast_read_identifier_compat(allocator, file, &current);
    if (!node->field) {
      node->field = neo_ast_read_private_name(allocator, file, &current);
    }
    if (!node->field) {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
      goto onerror;
    }
    NEO_CHECK_NODE(node->field, error, onerror);
    node->node.location.file = file;
    node->node.location.begin = *position;
    node->node.location.end = current;
    *position = current;
    return &node->node;
  } else if (neo_location_is(token->location, "?.")) {
    neo_allocator_free(allocator, token);
    if (*current.offset == '[') {
      current.offset++;
      current.column++;

      error = neo_skip_all(allocator, file, &current);
      if (error) {
        goto onerror;
      }
      node->field = neo_ast_read_expression(allocator, file, &current);
      if (!node->field) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
        goto onerror;
      }
      NEO_CHECK_NODE(node->field, error, onerror);
      error = neo_skip_all(allocator, file, &current);
      if (error) {
        goto onerror;
      }
      if (*current.offset != ']') {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
        goto onerror;
      }
      current.offset++;
      current.column++;
      node->node.type = NEO_NODE_TYPE_EXPRESSION_OPTIONAL_COMPUTED_MEMBER;
      node->node.location.file = file;
      node->node.location.begin = *position;
      node->node.location.end = current;
      *position = current;
      return &node->node;
    } else {
      node->field = neo_ast_read_identifier_compat(allocator, file, &current);
      if (!node->field) {
        node->field = neo_ast_read_private_name(allocator, file, &current);
      }
      if (!node->field) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
        goto onerror;
      }
      NEO_CHECK_NODE(node->field, error, onerror);
      node->node.type = NEO_NODE_TYPE_EXPRESSION_OPTIONAL_MEMBER;
      node->node.location.file = file;
      node->node.location.begin = *position;
      node->node.location.end = current;
      *position = current;
      return &node->node;
    }
  } else if (neo_location_is(token->location, "[")) {
    neo_allocator_free(allocator, token);

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    node->field = neo_ast_read_expression(allocator, file, &current);
    if (!node->field) {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
      goto onerror;
    }
    NEO_CHECK_NODE(node->field, error, onerror);
    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    if (*current.offset != ']') {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
      goto onerror;
    }
    current.offset++;
    current.column++;
    node->node.type = NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER;
    node->node.location.file = file;
    node->node.location.begin = *position;
    node->node.location.end = current;
    *position = current;
    return &node->node;
  }
onerror:
  neo_allocator_free(allocator, node);
  neo_allocator_free(allocator, token);
  return error;
}