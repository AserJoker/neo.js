#include "compiler/literal_template.h"
#include "compiler/expression.h"
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/position.h"
#include <stdio.h>

static void neo_ast_literal_template_dispose(neo_allocator_t allocator,
                                             neo_ast_literal_template_t node) {
  neo_allocator_free(allocator, node->expressions);
  neo_allocator_free(allocator, node->quasis);
  neo_allocator_free(allocator, node->tag);
}

static neo_ast_literal_template_t
neo_create_ast_literal_template(neo_allocator_t allocator) {
  neo_ast_literal_template_t node =
      neo_allocator_alloc2(allocator, neo_ast_literal_template);
  neo_list_initialize_t initialize = {true};
  node->expressions = neo_create_list(allocator, &initialize);
  node->quasis = neo_create_list(allocator, &initialize);
  node->tag = NULL;
  node->node.type = NEO_NODE_TYPE_LITERAL_TEMPLATE;
  return node;
}

neo_ast_node_t neo_ast_read_literal_template(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  if (*current.offset != '`') {
    return NULL;
  }
  neo_ast_literal_template_t node = neo_create_ast_literal_template(allocator);
  token = TRY(neo_read_template_string_token(allocator, file, &current)) {
    goto onerror;
  };
  if (!token) {
    goto onerror;
  }
  if (token->type == NEO_TOKEN_TYPE_TEMPLATE_STRING) {
    neo_list_push(node->quasis, token);
  } else {
    neo_list_push(node->quasis, token);
    SKIP_ALL(allocator, file, &current, onerror);
    for (;;) {
      neo_ast_node_t expr =
          TRY(neo_ast_read_expression(allocator, file, &current)) {
        goto onerror;
      };
      if (!expr) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
      neo_list_push(node->expressions, expr);
      SKIP_ALL(allocator, file, &current, onerror);
      token = TRY(neo_read_template_string_token(allocator, file, &current)) {
        goto onerror;
      };
      if (!token) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
      if (token->type == NEO_TOKEN_TYPE_TEMPLATE_STRING_END) {
        neo_list_push(node->quasis, token);
        break;
      }
      neo_list_push(node->quasis, token);
      SKIP_ALL(allocator, file, &current, onerror);
    }
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