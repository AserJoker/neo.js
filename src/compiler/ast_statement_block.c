#include "neojs/compiler/ast_statement_block.h"
#include "neojs/compiler/ast_node.h"
#include "neojs/compiler/ast_statement.h"
#include "neojs/compiler/scope.h"
#include "neojs/core/allocator.h"
#include "neojs/core/any.h"
#include "neojs/core/list.h"
#include "neojs/core/position.h"
#include <stdio.h>

static void neo_ast_statement_block_dispose(neo_allocator_t allocator,
                                            neo_ast_statement_block_t node) {
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->node.scope);
}
static void neo_ast_statement_block_write(neo_allocator_t allocator,
                                          neo_write_context_t ctx,
                                          neo_ast_statement_block_t self) {
  neo_writer_push_scope(allocator, ctx, self->node.scope);
  for (neo_list_node_t it = neo_list_get_first(self->body);
       it != neo_list_get_tail(self->body); it = neo_list_node_next(it)) {
    neo_ast_node_t item = neo_list_node_get(it);
    item->write(allocator, ctx, item);
  }
  neo_writer_pop_scope(allocator, ctx, self->node.scope);
}
static void
neo_ast_statement_block_resolve_closure(neo_allocator_t allocator,
                                        neo_ast_statement_block_t self,
                                        neo_list_t closure) {
  neo_compile_scope_t scope = neo_compile_scope_set(self->node.scope);
  for (neo_list_node_t it = neo_list_get_first(self->body);
       it != neo_list_get_tail(self->body); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
  neo_compile_scope_pop(scope);
}

static neo_any_t
neo_serialize_ast_statement_block(neo_allocator_t allocator,
                                  neo_ast_statement_block_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(
      variable, "type",
      neo_create_any_string(allocator, "NEO_NODE_TYPE_STATEMENT_BLOCK"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "body",
              neo_ast_node_list_serialize(allocator, node->body));
  return variable;
}
static neo_ast_statement_block_t
neo_create_statement_block(neo_allocator_t allocator) {
  neo_ast_statement_block_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_statement_block_t),
                          neo_ast_statement_block_dispose);
  node->node.type = NEO_NODE_TYPE_STATEMENT_BLOCK;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_statement_block;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_statement_block_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_statement_block_write;
  neo_list_initialize_t initialize = {0};
  initialize.auto_free = true;
  node->body = neo_create_list(allocator, &initialize);
  return node;
}

neo_ast_node_t neo_ast_read_statement_block(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position) {
  neo_position_t current = *position;
  neo_compile_scope_t scope = NULL;
  neo_ast_node_t error = NULL;
  if (*current.offset != '{') {
    return NULL;
  }
  neo_ast_statement_block_t node = neo_create_statement_block(allocator);
  current.offset++;
  current.column++;
  scope =
      neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_BLOCK, false, false);

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  while (true) {
    neo_ast_node_t statement =
        neo_ast_read_statement(allocator, file, &current);
    if (!statement) {
      break;
    }
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

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset != '}') {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  node->node.scope = neo_compile_scope_pop(scope);
  *position = current;
  return &node->node;
onerror:
  if (scope && !node->node.scope) {
    scope = neo_compile_scope_pop(scope);
    neo_allocator_free(allocator, scope);
  }
  neo_allocator_free(allocator, node);
  return error;
}