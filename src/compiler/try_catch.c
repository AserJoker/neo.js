#include "compiler/try_catch.h"
#include "compiler/identifier.h"
#include "compiler/pattern_array.h"
#include "compiler/pattern_object.h"
#include "compiler/statement_block.h"
#include "compiler/token.h"
#include <stdio.h>

static void neo_ast_try_catch_dispose(neo_allocator_t allocator,
                                      neo_ast_try_catch_t node) {
  neo_allocator_free(allocator, node->error);
  neo_allocator_free(allocator, node->body);
}

static neo_ast_try_catch_t neo_create_ast_try_catch(neo_allocator_t allocator) {
  neo_ast_try_catch_t node = neo_allocator_alloc2(allocator, neo_ast_try_catch);
  node->node.type = NEO_NODE_TYPE_TRY_CATCH;
  node->body = NULL;
  node->error = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_try_catch(neo_allocator_t allocator,
                                      const char *file,
                                      neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_try_catch_t node = neo_create_ast_try_catch(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "catch")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset == '(') {
    current.column++;
    current.offset++;
    SKIP_ALL(allocator, file, &current, onerror);
    node->error = TRY(neo_ast_read_pattern_object(allocator, file, &current)) {
      goto onerror;
    };
    if (!node->error) {
      node->error = TRY(neo_ast_read_pattern_array(allocator, file, &current)) {
        goto onerror;
      }
    }
    if (!node->error) {
      node->error = TRY(neo_ast_read_identifier(allocator, file, &current)) {
        goto onerror;
      }
    }
    if (!node->error) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
    SKIP_ALL(allocator, file, &current, onerror);
    if (*current.offset != ')') {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
    current.offset++;
    current.column++;
    SKIP_ALL(allocator, file, &current, onerror);
  }
  node->body = TRY(neo_ast_read_statement_block(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->body) {
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