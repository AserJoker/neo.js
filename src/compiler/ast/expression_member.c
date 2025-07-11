#include "compiler/ast/expression_member.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/node.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
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
  TRY(neo_write_optional_chain(allocator, ctx, &self->node, addresses)) {
    neo_allocator_free(allocator, addresses);
    return;
  }
  for (neo_list_node_t it = neo_list_get_first(addresses);
       it != neo_list_get_tail(addresses); it = neo_list_node_next(it)) {
    size_t *address = neo_list_node_get(it);
    neo_program_set_current(ctx->program, *address);
  }
  neo_allocator_free(allocator, addresses);
}

static neo_variable_t
neo_serialize_ast_expression_member(neo_allocator_t allocator,
                                    neo_ast_expression_member_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  if (node->node.type == NEO_NODE_TYPE_EXPRESSION_MEMBER) {
    neo_variable_set(variable, L"type",
                     neo_create_variable_string(
                         allocator, L"NEO_NODE_TYPE_EXPRESSION_MEMBER"));
  } else if (node->node.type == NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER) {
    neo_variable_set(
        variable, L"type",
        neo_create_variable_string(allocator,
                                   L"NEO_NODE_TYPE_EXPRESSION_COMPUTED_MEMBER"));
  } else if (node->node.type == NEO_NODE_TYPE_EXPRESSION_OPTIONAL_MEMBER) {
    neo_variable_set(
        variable, L"type",
        neo_create_variable_string(allocator,
                                   L"NEO_NODE_TYPE_EXPRESSION_OPTIONAL_MEMBER"));
  } else if (node->node.type ==
             NEO_NODE_TYPE_EXPRESSION_OPTIONAL_COMPUTED_MEMBER) {
    neo_variable_set(
        variable, L"type",
        neo_create_variable_string(
            allocator, L"NEO_NODE_TYPE_EXPRESSION_OPTIONAL_COMPUTED_MEMBER"));
  }
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, L"host",
                   neo_ast_node_serialize(allocator, node->host));
  neo_variable_set(variable, L"field",
                   neo_ast_node_serialize(allocator, node->field));
  return variable;
}

static neo_ast_expression_member_t
neo_create_ast_expression_member(neo_allocator_t allocator) {
  neo_ast_expression_member_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_member);
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
                                              const wchar_t *file,
                                              neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_expression_member_t node =
      neo_create_ast_expression_member(allocator);
  token = TRY(neo_read_symbol_token(allocator, file, &current)) {
    goto onerror;
  }
  if (!token) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (neo_location_is(token->location, ".")) {
    neo_allocator_free(allocator, token);
    node->field =
        TRY(neo_ast_read_identifier_compat(allocator, file, &current)) {
      goto onerror;
    }
    if (!node->field) {
      THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
            current.line, current.column);
      goto onerror;
    }
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
      SKIP_ALL(allocator, file, &current, onerror);
      node->field = TRY(neo_ast_read_expression(allocator, file, &current)) {
        goto onerror;
      };
      if (!node->field) {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
              current.line, current.column);
        goto onerror;
      }
      SKIP_ALL(allocator, file, &current, onerror);
      if (*current.offset != ']') {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
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
      node->field = TRY(neo_ast_read_identifier(allocator, file, &current)) {
        goto onerror;
      }
      if (!node->field) {
        goto onerror;
      }
      node->node.type = NEO_NODE_TYPE_EXPRESSION_OPTIONAL_MEMBER;
      node->node.location.file = file;
      node->node.location.begin = *position;
      node->node.location.end = current;
      *position = current;
      return &node->node;
    }
  } else if (neo_location_is(token->location, "[")) {
    neo_allocator_free(allocator, token);
    SKIP_ALL(allocator, file, &current, onerror);
    node->field = TRY(neo_ast_read_expression(allocator, file, &current)) {
      goto onerror;
    };
    if (!node->field) {
      THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
            current.line, current.column);
      goto onerror;
    }
    SKIP_ALL(allocator, file, &current, onerror);
    if (*current.offset != ']') {
      THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
            current.line, current.column);
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
  return NULL;
}