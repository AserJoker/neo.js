#include "compiler/ast/declaration_variable.h"
#include "compiler/ast/node.h"
#include "compiler/ast/variable_declarator.h"
#include "compiler/token.h"
#include "core/list.h"
#include "core/variable.h"
#include <stdio.h>

static void
neo_ast_declaration_variable_dispose(neo_allocator_t allocator,
                                     neo_ast_declaration_variable_t node) {
  neo_allocator_free(allocator, node->declarators);
  neo_allocator_free(allocator, node->node.scope);
}

static void neo_ast_declaration_variable(neo_allocator_t allocator,
                                         neo_ast_declaration_variable_t node,
                                         neo_list_t closure) {
  for (neo_list_node_t it = neo_list_get_first(node->declarators);
       it != neo_list_get_tail(node->declarators);
       it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
}
static void
neo_ast_declaration_variable_write(neo_allocator_t allocator,
                                   neo_write_context_t ctx,
                                   neo_ast_declaration_variable_t self) {
  for (neo_list_node_t it = neo_list_get_first(self->declarators);
       it != neo_list_get_tail(self->declarators);
       it = neo_list_node_next(it)) {
    neo_ast_node_t item = neo_list_node_get(it);
    TRY(item->write(allocator, ctx, item)) { return; }
  }
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
  case NEO_AST_DECLARATION_USING:
    neo_variable_set(
        variable, "kind",
        neo_create_variable_string(allocator, "NEO_AST_DECLARATION_USING"));
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
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_declaration_variable_t
neo_create_ast_declaration_variable(neo_allocator_t allocator) {
  neo_ast_declaration_variable_t node =
      neo_allocator_alloc2(allocator, neo_ast_declaration_variable);
  neo_list_initialize_t initialize = {true};
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_declaration_variable;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_declaration_variable;
  node->node.write = (neo_write_fn_t)neo_ast_declaration_variable_write;
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
  } else if (neo_location_is(token->location, "using")) {
    node->kind = NEO_AST_DECLARATION_USING;
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
    switch (node->kind) {
    case NEO_AST_DECLARATION_VAR:
      TRY(neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                   declarator, NEO_COMPILE_VARIABLE_VAR)) {
        goto onerror;
      };
      break;
    case NEO_AST_DECLARATION_CONST:
      TRY(neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                   declarator, NEO_COMPILE_VARIABLE_CONST)) {
        goto onerror;
      };
      break;
    case NEO_AST_DECLARATION_USING:
      TRY(neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                   declarator, NEO_COMPILE_VARIABLE_USING)) {
        goto onerror;
      };
      break;
    case NEO_AST_DECLARATION_LET:
      TRY(neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                   declarator, NEO_COMPILE_VARIABLE_LET)) {
        goto onerror;
      };
      break;
    case NEO_AST_DECLARATION_NONE:
      break;
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