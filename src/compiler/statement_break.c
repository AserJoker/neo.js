#include "compiler/statement_break.h"
#include "compiler/identifier.h"
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/location.h"
#include "core/position.h"
#include <stdio.h>
static void neo_ast_statement_break_dispose(neo_allocator_t allocator,
                                            neo_ast_statement_break_t node) {
  neo_allocator_free(allocator, node->label);
}

static neo_ast_statement_break_t
neo_create_ast_statement_break(neo_allocator_t allocator) {
  neo_ast_statement_break_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_break);
  node->label = NULL;
  node->node.type = NEO_NODE_TYPE_STATEMENT_BREAK;
  return node;
}

neo_ast_node_t neo_ast_read_statement_break(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_statement_break_t node = neo_create_ast_statement_break(allocator);
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "break")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  uint32_t line = current.line;
  neo_position_t cur = current;
  SKIP_ALL(allocator, file, &cur, onerror);
  if (cur.line == line) {
    node->label = TRY(neo_ast_read_identifier(allocator, file, &cur)) {
      goto onerror;
    };
    if (node->label) {
      current = cur;
      line = cur.line;
      SKIP_ALL(allocator, file, &cur, onerror);
    }
  }
  if (*cur.offset != '}' && *cur.offset != ';' && line == cur.line) {
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
  neo_allocator_free(allocator, token);
  return NULL;
}