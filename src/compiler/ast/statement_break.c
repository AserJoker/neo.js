#include "compiler/ast/statement_break.h"
#include "compiler/asm.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>
static void neo_ast_statement_break_dispose(neo_allocator_t allocator,
                                            neo_ast_statement_break_t node) {
  neo_allocator_free(allocator, node->label);
  neo_allocator_free(allocator, node->node.scope);
}
static void neo_ast_statement_break_write(neo_allocator_t allocator,
                                          neo_write_context_t ctx,
                                          neo_ast_statement_break_t self) {
  neo_program_add_code(allocator, ctx->program, NEO_ASM_BREAK);
  if (self->label) {
    wchar_t *label = neo_location_get(allocator, self->label->location);
    neo_program_add_string(allocator, ctx->program, label);
    neo_allocator_free(allocator, label);
  } else {
    neo_program_add_string(allocator, ctx->program, L"");
  }
}
static neo_variable_t
neo_serialize_ast_statement_break(neo_allocator_t allocator,
                                  neo_ast_statement_break_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, L"type",
      neo_create_variable_string(allocator, L"NEO_NODE_TYPE_STATEMENT_BREAK"));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, L"label",
                   neo_ast_node_serialize(allocator, node->label));
  return variable;
}
static neo_ast_statement_break_t
neo_create_ast_statement_break(neo_allocator_t allocator) {
  neo_ast_statement_break_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_statement_break_t),
                          neo_ast_statement_break_dispose);
  node->label = NULL;
  node->node.type = NEO_NODE_TYPE_STATEMENT_BREAK;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_statement_break;
  node->node.resolve_closure = neo_ast_node_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_statement_break_write;
  return node;
}

neo_ast_node_t neo_ast_read_statement_break(neo_allocator_t allocator,
                                            const wchar_t *file,
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
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
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