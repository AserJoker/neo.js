#include "compiler/ast/expression_assigment.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/node.h"
#include "compiler/ast/pattern_array.h"
#include "compiler/ast/pattern_object.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdbool.h>
#include <stdio.h>

static void
neo_ast_expression_assigment_dispose(neo_allocator_t allocator,
                                     neo_ast_expression_assigment_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->value);
  neo_allocator_free(allocator, node->opt);
  neo_allocator_free(allocator, node->node.scope);
}

static void neo_ast_expression_assigment_resolve_closure(
    neo_allocator_t allocator, neo_ast_expression_assigment_t self,
    neo_list_t closure) {
  self->identifier->resolve_closure(allocator, self->identifier, closure);
  self->identifier->resolve_closure(allocator, self->value, closure);
}

static neo_variable_t
neo_serialize_ast_expression_assigment(neo_allocator_t allocator,
                                       neo_ast_expression_assigment_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_EXPRESSION_ASSIGMENT"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  neo_variable_set(variable, "value",
                   neo_ast_node_serialize(allocator, node->value));
  return variable;
}

static neo_ast_expression_assigment_t
neo_create_ast_expression_assigment(neo_allocator_t allocator) {
  neo_ast_expression_assigment_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_assigment);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_ASSIGMENT;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_expression_assigment;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_expression_assigment_resolve_closure;
  node->identifier = NULL;
  node->value = NULL;
  node->opt = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_expression_assigment(neo_allocator_t allocator,
                                                 const char *file,
                                                 neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_assigment_t node = NULL;
  neo_token_t token = NULL;
  node = neo_create_ast_expression_assigment(allocator);
  node->identifier = TRY(neo_ast_read_identifier(allocator, file, &current)) {
    goto onerror;
  };
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_pattern_object(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_pattern_array(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  token = TRY(neo_read_symbol_token(allocator, file, &current)) {
    goto onerror;
  };
  if (!token) {
    goto onerror;
  }
  if (!neo_location_is(token->location, "=") &&
      !neo_location_is(token->location, "+=") &&
      !neo_location_is(token->location, "-=") &&
      !neo_location_is(token->location, "**=") &&
      !neo_location_is(token->location, "*=") &&
      !neo_location_is(token->location, "/=") &&
      !neo_location_is(token->location, "%=") &&
      !neo_location_is(token->location, "<<=") &&
      !neo_location_is(token->location, ">>=") &&
      !neo_location_is(token->location, ">>>=") &&
      !neo_location_is(token->location, "&=") &&
      !neo_location_is(token->location, "|=") &&
      !neo_location_is(token->location, "^=") &&
      !neo_location_is(token->location, "&&=") &&
      !neo_location_is(token->location, "||=") &&
      !neo_location_is(token->location, R"(??=)")) {
    neo_allocator_free(allocator, token);
    goto onerror;
  }
  node->opt = token;
  SKIP_ALL(allocator, file, &current, onerror);
  node->value = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
    goto onerror;
  };
  if (!node->value) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}