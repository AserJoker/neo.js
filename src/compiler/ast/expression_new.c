#include "compiler/ast/expression_new.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/expression_call.h"
#include "compiler/ast/expression_member.h"
#include "compiler/ast/literal_template.h"
#include "compiler/ast/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_expression_new_dispose(neo_allocator_t allocator,
                                           neo_ast_expression_new_t node) {
  neo_allocator_free(allocator, node->arguments);
  neo_allocator_free(allocator, node->callee);
  neo_allocator_free(allocator, node->node.scope);
}

static neo_variable_t
neo_serialize_ast_expression_new(neo_allocator_t allocator,
                                 neo_ast_expression_new_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_EXPRESSION_NEW"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "name",
                   neo_ast_node_serialize(allocator, node->callee));
  neo_variable_set(variable, "arguments",
                   neo_ast_node_list_serialize(allocator, node->arguments));
  return variable;
}

static neo_ast_expression_new_t
neo_create_ast_expression_new(neo_allocator_t allocator) {
  neo_ast_expression_new_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_new);
  node->callee = NULL;
  neo_list_initialize_t initialize = {true};
  node->arguments = neo_create_list(allocator, &initialize);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_NEW;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_expression_new;
  return node;
}

neo_ast_node_t neo_ast_read_expression_new(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_new_t node = NULL;
  neo_ast_node_t callee = NULL;
  neo_token_t token = NULL;
  node = neo_create_ast_expression_new(allocator);
  token = TRY(neo_read_identify_token(allocator, file, &current)) {
    goto onerror;
  };
  if (!token || !neo_location_is(token->location, "new")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->callee = TRY(neo_ast_read_expression_new(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->callee) {
    node->callee = TRY(neo_ast_read_expression_18(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->callee) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  for (;;) {
    neo_ast_node_t bnode = NULL;
    bnode = TRY(neo_ast_read_expression_member(allocator, file, &current)) {
      goto onerror;
    };
    if (bnode) {
      ((neo_ast_expression_member_t)bnode)->host = node->callee;
    }
    if (!bnode) {
      bnode = TRY(neo_ast_read_literal_template(allocator, file, &current)) {
        goto onerror;
      };
      if (bnode) {
        ((neo_ast_literal_template_t)bnode)->tag = node->callee;
      }
    }
    if (!bnode) {
      break;
    }
    node->callee = bnode;
    node->callee->location.begin = *position;
    node->callee->location.end = current;
    node->callee->location.file = file;
    SKIP_ALL(allocator, file, &current, onerror);
  }
  neo_position_t cur = current;
  SKIP_ALL(allocator, file, &cur, onerror);
  if (*cur.offset == '(') {
    current = cur;
    neo_ast_expression_call_t call = (neo_ast_expression_call_t)TRY(
        neo_ast_read_expression_call(allocator, file, &current)) {
      goto onerror;
    };
    if (!call) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
    neo_allocator_free(allocator, node->arguments);
    node->arguments = call->arguments;
    call->arguments = NULL;
    neo_allocator_free(allocator, call);
  }
  node->node.location.file = file;
  node->node.location.begin = *position;
  node->node.location.end = current;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, callee);
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}