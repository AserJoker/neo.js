#include "compiler/ast/statement_labeled.h"
#include "compiler/asm.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/node.h"
#include "compiler/ast/statement.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>
static void
neo_ast_statement_labeled_dispose(neo_allocator_t allocator,
                                  neo_ast_statement_labeled_t node) {
  neo_allocator_free(allocator, node->label);
  neo_allocator_free(allocator, node->statement);
  neo_allocator_free(allocator, node->node.scope);
}
static void
neo_ast_statement_label_resolve_closure(neo_allocator_t allocator,
                                        neo_ast_statement_labeled_t self,
                                        neo_list_t closure) {
  self->statement->resolve_closure(allocator, self->statement, closure);
}
static void neo_ast_statement_labeled_write(neo_allocator_t allocator,
                                            neo_write_context_t ctx,
                                            neo_ast_statement_labeled_t self) {
  if (self->statement->type == NEO_NODE_TYPE_STATEMENT_FOR ||
      self->statement->type == NEO_NODE_TYPE_STATEMENT_FOR_OF ||
      self->statement->type == NEO_NODE_TYPE_STATEMENT_FOR_IN ||
      self->statement->type == NEO_NODE_TYPE_STATEMENT_FOR_AWAIT_OF ||
      self->statement->type == NEO_NODE_TYPE_STATEMENT_WHILE ||
      self->statement->type == NEO_NODE_TYPE_STATEMENT_DO_WHILE) {
    ctx->label = neo_location_get(allocator, self->label->location);
    TRY(self->statement->write(allocator, ctx, self->statement)) { return; }
    neo_allocator_free_ex(allocator, ctx->label);
  } else if (self->statement->type == NEO_NODE_TYPE_DECLARATION_FUNCTION) {
    THROW("functions cannot be labelled");
  } else if (self->statement->type == NEO_NODE_TYPE_DECLARATION_CLASS) {
    THROW("classes cannot be labelled");
  } else if (self->statement->type == NEO_NODE_TYPE_DECLARATION_VARIABLE) {
    THROW("variable declaration cannot be labelled");
  } else if (self->statement->type == NEO_NODE_TYPE_DECLARATION_IMPORT) {
    THROW("import declaration cannot be labelled");
  } else if (self->statement->type == NEO_NODE_TYPE_DECLARATION_EXPORT) {
    THROW("export declaration cannot be labelled");
  } else {
    wchar_t *label = neo_location_get(allocator, self->label->location);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_BREAK_LABEL);
    neo_program_add_string(allocator, ctx->program, label);
    size_t breakaddr = neo_buffer_get_size(ctx->program->codes);
    neo_program_add_address(allocator, ctx->program, 0);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_CONTINUE_LABEL);
    neo_program_add_string(allocator, ctx->program, label);
    size_t continueaddr = neo_buffer_get_size(ctx->program->codes);
    neo_program_add_address(allocator, ctx->program, 0);
    TRY(self->statement->write(allocator, ctx, self->statement)) { return; }
    neo_program_set_current(ctx->program, continueaddr);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP_LABEL);
    neo_program_set_current(ctx->program, breakaddr);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP_LABEL);
    neo_allocator_free_ex(allocator, label);
  }
}
static neo_variable_t
neo_serialize_ast_statement_labeled(neo_allocator_t allocator,
                                    neo_ast_statement_labeled_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, L"type",
      neo_create_variable_string(allocator, L"NEO_NODE_TYPE_STATEMENT_LABELED"));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, L"label",
                   neo_ast_node_serialize(allocator, node->label));
  neo_variable_set(variable, L"statement",
                   neo_ast_node_serialize(allocator, node->statement));
  return variable;
}
static neo_ast_statement_labeled_t
neo_create_ast_statement_labeled(neo_allocator_t allocator) {
  neo_ast_statement_labeled_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_labeled);
  node->node.type = NEO_NODE_TYPE_STATEMENT_LABELED;

  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_statement_labeled;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_statement_label_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_statement_labeled_write;
  node->label = NULL;
  node->statement = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_statement_labeled(neo_allocator_t allocator,
                                              const wchar_t *file,
                                              neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_statement_labeled_t node =
      neo_create_ast_statement_labeled(allocator);
  node->label = neo_ast_read_identifier(allocator, file, &current);
  if (!node->label) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  token = neo_read_symbol_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, ":")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->statement = TRY(neo_ast_read_statement(allocator, file, &current)) {
    goto onerror;
  };
  if (!node->statement) {
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
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}
