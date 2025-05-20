#include "compiler/statement_try.h"
#include "compiler/statement_block.h"
#include "compiler/token.h"
#include "compiler/try_catch.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_statement_try_dispose(neo_allocator_t allocator,
                                          neo_ast_statement_try_t node) {
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->catch);
  neo_allocator_free(allocator, node->finally);
}
static neo_variable_t
neo_serialize_ast_statement_try(neo_allocator_t allocator,
                                neo_ast_statement_try_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_STATEMENT_TRY"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "body",
                   neo_ast_node_serialize(allocator, node->body));
  neo_variable_set(variable, "catch",
                   neo_ast_node_serialize(allocator, node->catch));
  neo_variable_set(variable, "finally",
                   neo_ast_node_serialize(allocator, node->finally));
  return variable;
}
static neo_ast_statement_try_t
neo_create_ast_statement_try(neo_allocator_t allocator) {
  neo_ast_statement_try_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_try);
  node->node.type = NEO_NODE_TYPE_STATEMENT_TRY;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_statement_try;
  node->body = NULL;
  node->catch = NULL;
  node->finally = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_statement_try(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_statement_try_t node = neo_create_ast_statement_try(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "try")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->body = TRY(neo_ast_read_statement_block(allocator, file, &current)) {
    goto onerror;
  };
  if (!node->body) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  neo_position_t cur = current;
  SKIP_ALL(allocator, file, &cur, onerror);
  node->catch = TRY(neo_ast_read_try_catch(allocator, file, &cur)) {
    goto onerror;
  };
  if (node->catch) {
    current = cur;
  }
  SKIP_ALL(allocator, file, &cur, onerror);
  token = neo_read_identify_token(allocator, file, &cur);
  if (token && neo_location_is(token->location, "finally")) {
    SKIP_ALL(allocator, file, &cur, onerror);
    node->finally = TRY(neo_ast_read_statement_block(allocator, file, &cur)) {
      goto onerror;
    };
    if (!node->finally) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            cur.line, cur.column);
      goto onerror;
    }
    current = cur;
  }
  neo_allocator_free(allocator, token);
  if (!node->catch && !node->finally) {
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
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}