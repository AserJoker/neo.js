#include "compiler/ast/declaration_variable.h"
#include "compiler/ast/node.h"
#include "compiler/ast/variable_declarator.h"
#include "compiler/token.h"
#include "core/any.h"
#include "core/list.h"
#include "core/location.h"
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
    item->write(allocator, ctx, item);
  }
}
static neo_any_t
neo_serialize_ast_declaration_variable(neo_allocator_t allocator,
                                       neo_ast_declaration_variable_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(
      variable, "type",
      neo_create_any_string(allocator, "NEO_NODE_TYPE_DECLARATION_VARIABLE"));
  neo_any_set(variable, "declarators",
              neo_ast_node_list_serialize(allocator, node->declarators));
  switch (node->kind) {
  case NEO_AST_DECLARATION_VAR:
    neo_any_set(variable, "kind",
                neo_create_any_string(allocator, "NEO_AST_DECLARATION_VAR"));
    break;
  case NEO_AST_DECLARATION_CONST:
    neo_any_set(variable, "kind",
                neo_create_any_string(allocator, "NEO_AST_DECLARATION_CONST"));
    break;
  case NEO_AST_DECLARATION_USING:
    neo_any_set(variable, "kind",
                neo_create_any_string(allocator, "NEO_AST_DECLARATION_USING"));
    break;
  case NEO_AST_DECLARATION_AWAIT_USING:
    neo_any_set(
        variable, "kind",
        neo_create_any_string(allocator, "NEO_AST_DECLARATION_AWAIT_USING"));
    break;
  case NEO_AST_DECLARATION_LET:
    neo_any_set(variable, "kind",
                neo_create_any_string(allocator, "NEO_AST_DECLARATION_LET"));
    break;
  case NEO_AST_DECLARATION_NONE:
    neo_any_set(variable, "kind",
                neo_create_any_string(allocator, "NEO_AST_DECLARATION_NONE"));
    break;
  }
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_declaration_variable_t
neo_create_ast_declaration_variable(neo_allocator_t allocator) {
  neo_ast_declaration_variable_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_declaration_variable_t),
      neo_ast_declaration_variable_dispose);
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
  neo_ast_node_t error = NULL;
  neo_ast_declaration_variable_t node =
      neo_create_ast_declaration_variable(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (!token) {
    goto onerror;
  }
  if (neo_location_is(token->location, "await")) {
    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    neo_allocator_free(allocator, token);
    token = neo_read_identify_token(allocator, file, &current);
    if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
      error = neo_create_error_node(allocator, NULL);
      error->error = token->error;
      token->error = NULL;
      goto onerror;
    }
    if (!token) {
      goto onerror;
    }
    if (neo_location_is(token->location, "using")) {
      if (!neo_compile_scope_is_async()) {
        error = neo_create_error_node(
            allocator,
            "await using only used in async context\n  at _.compile "
            "(%s:%d:%d)",
            file, position->line, position->column);
        goto onerror;
      }
      node->kind = NEO_AST_DECLARATION_AWAIT_USING;
    } else {
      goto onerror;
    }
  } else if (neo_location_is(token->location, "const")) {
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

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  for (;;) {
    neo_ast_node_t declarator =
        neo_ast_read_variable_declarator(allocator, file, &current);
    if (!declarator) {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
      goto onerror;
    }
    NEO_CHECK_NODE(declarator, error, onerror);
    neo_list_push(node->declarators, declarator);
    switch (node->kind) {
    case NEO_AST_DECLARATION_VAR:
      error =
          neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                   declarator, NEO_COMPILE_VARIABLE_VAR);
      if (error) {
        goto onerror;
      };
      break;
    case NEO_AST_DECLARATION_CONST:
      error =
          neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                   declarator, NEO_COMPILE_VARIABLE_CONST);
      if (error) {
        goto onerror;
      };
      break;
    case NEO_AST_DECLARATION_USING:
      error =
          neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                   declarator, NEO_COMPILE_VARIABLE_USING);
      if (error) {
        goto onerror;
      };
      break;
    case NEO_AST_DECLARATION_AWAIT_USING:
      error = neo_compile_scope_declar(
          allocator, neo_compile_scope_get_current(), declarator,
          NEO_COMPILE_VARIABLE_AWAIT_USING);
      if (error) {
        goto onerror;
      };
      break;
    case NEO_AST_DECLARATION_LET:
      error =
          neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                   declarator, NEO_COMPILE_VARIABLE_LET);
      if (error) {
        goto onerror;
      };
      break;
    case NEO_AST_DECLARATION_NONE:
      break;
    }
    neo_position_t cur = current;
    error = neo_skip_all(allocator, file, &cur);
    if (error) {
      goto onerror;
    }
    if (*cur.offset == ',') {
      current = cur;
      current.offset++;
      current.column++;

      error = neo_skip_all(allocator, file, &current);
      if (error) {
        goto onerror;
      }
    } else {
      break;
    }
  }
  uint32_t line = current.line;
  neo_position_t cur = current;
  error = neo_skip_all(allocator, file, &cur);
  if (error) {
    goto onerror;
  }
  if (cur.line == line) {
    if (*cur.offset && *cur.offset != ';' && *cur.offset != '}') {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, cur.line, cur.column);
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
  return error;
}