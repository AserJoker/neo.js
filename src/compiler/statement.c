#include "compiler/statement.h"
#include "compiler/statement_block.h"
#include "compiler/statement_break.h"
#include "compiler/statement_continue.h"
#include "compiler/statement_debugger.h"
#include "compiler/statement_empty.h"
#include "compiler/statement_expression.h"
#include "compiler/statement_if.h"
#include "compiler/statement_labeled.h"
#include "compiler/statement_return.h"
#include "compiler/statement_switch.h"
#include "core/error.h"
neo_ast_node_t neo_ast_read_statement(neo_allocator_t allocator,
                                      const char *file,
                                      neo_position_t *position) {
  neo_ast_node_t node = NULL;
  node = TRY(neo_ast_read_statement_empty(allocator, file, position)) {
    goto onerror;
  }
  if (!node) {
    node = TRY(neo_ast_read_statement_block(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_statement_debugger(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_statement_break(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_statement_continue(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_statement_return(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_statement_if(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_statement_switch(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_statement_labeled(allocator, file, position)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_statement_expression(allocator, file, position)) {
      goto onerror;
    }
  }
  return node;
onerror:
  return NULL;
}
