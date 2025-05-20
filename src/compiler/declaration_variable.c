#include "compiler/declaration_variable.h"
#include "compiler/token.h"
#include "compiler/variable_declarator.h"
#include "core/variable.h"
#include <stdio.h>

static void
neo_ast_declaration_variable_dispose(neo_allocator_t allocator,
                                     neo_ast_declaration_variable_t node) {
  neo_allocator_free(allocator, node->declarators);
  neo_allocator_free(allocator, node->node.scope);
}
static neo_variable_t
neo_serialize_ast_declaration_variable(neo_allocator_t allocator,
                                       neo_ast_declaration_variable_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_DECLARATION_VARIABLE"));
  neo_variable_set(variable, "declarators",
                   neo_ast_node_list_serialize(allocator, node->declarators));
  switch (node->kind) {
  case NEO_AST_DECLARATION_VAR:
    neo_variable_set(
        variable, "kind",
        neo_create_variable_string(allocator, "NEO_AST_DECLARATION_VAR"));
    break;
  case NEO_AST_DECLARATION_CONST:
    neo_variable_set(
        variable, "kind",
        neo_create_variable_string(allocator, "NEO_AST_DECLARATION_CONST"));
    break;
  case NEO_AST_DECLARATION_LET:
    neo_variable_set(
        variable, "kind",
        neo_create_variable_string(allocator, "NEO_AST_DECLARATION_LET"));
    break;
  case NEO_AST_DECLARATION_NONE:
    neo_variable_set(
        variable, "kind",
        neo_create_variable_string(allocator, "NEO_AST_DECLARATION_NONE"));
    break;
  }
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  return variable;
}

static neo_ast_declaration_variable_t
neo_create_ast_declaration_variable(neo_allocator_t allocator) {
  neo_ast_declaration_variable_t node =
      neo_allocator_alloc2(allocator, neo_ast_declaration_variable);
  neo_list_initialize_t initialize = {true};
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn)neo_serialize_ast_declaration_variable;
  node->node.type = NEO_NODE_TYPE_DECLARATION_VARIABLE;
  node->declarators = neo_create_list(allocator, &initialize);
  node->kind = NEO_AST_DECLARATION_VAR;
  return node;
}

neo_ast_node_t neo_ast_read_declaration_variable(neo_allocator_t allocator,
                                                 const char *file,
                                                 neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_declaration_variable_t node =
      neo_create_ast_declaration_variable(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token) {
    goto onerror;
  }
  if (neo_location_is(token->location, "const")) {
    node->kind = NEO_AST_DECLARATION_CONST;
  } else if (neo_location_is(token->location, "let")) {
    node->kind = NEO_AST_DECLARATION_LET;
  } else if (neo_location_is(token->location, "var")) {
    node->kind = NEO_AST_DECLARATION_VAR;
  } else {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  for (;;) {
    neo_ast_node_t declarator =
        TRY(neo_ast_read_variable_declarator(allocator, file, &current)) {
      goto onerror;
    };
    if (!declarator) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
    }
    neo_list_push(node->declarators, declarator);
    neo_position_t cur = current;
    SKIP_ALL(allocator, file, &cur, onerror);
    if (*cur.offset == ',') {
      current = cur;
      current.offset++;
      current.column++;
      SKIP_ALL(allocator, file, &current, onerror);
    } else {
      break;
    }
  }
  uint32_t line = current.line;
  neo_position_t cur = current;
  SKIP_ALL(allocator, file, &cur, onerror);
  if (cur.line == line) {
    if (*cur.offset && *cur.offset != ';' && *cur.offset != '}') {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            cur.line, cur.column);
      goto onerror;
    }
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  neo_allocator_free(allocator, token);
  return NULL;
}