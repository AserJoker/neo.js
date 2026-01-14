#include "neo.js/compiler/ast_statement.h"
#include "neo.js/compiler/ast_declaration_class.h"
#include "neo.js/compiler/ast_declaration_export.h"
#include "neo.js/compiler/ast_declaration_function.h"
#include "neo.js/compiler/ast_declaration_import.h"
#include "neo.js/compiler/ast_declaration_variable.h"
#include "neo.js/compiler/ast_statement_block.h"
#include "neo.js/compiler/ast_statement_break.h"
#include "neo.js/compiler/ast_statement_continue.h"
#include "neo.js/compiler/ast_statement_debugger.h"
#include "neo.js/compiler/ast_statement_do_while.h"
#include "neo.js/compiler/ast_statement_empty.h"
#include "neo.js/compiler/ast_statement_expression.h"
#include "neo.js/compiler/ast_statement_for.h"
#include "neo.js/compiler/ast_statement_for_await_of.h"
#include "neo.js/compiler/ast_statement_for_in.h"
#include "neo.js/compiler/ast_statement_for_of.h"
#include "neo.js/compiler/ast_statement_if.h"
#include "neo.js/compiler/ast_statement_labeled.h"
#include "neo.js/compiler/ast_statement_return.h"
#include "neo.js/compiler/ast_statement_switch.h"
#include "neo.js/compiler/ast_statement_throw.h"
#include "neo.js/compiler/ast_statement_try.h"
#include "neo.js/compiler/ast_statement_while.h"

neo_ast_node_t neo_ast_read_statement(neo_allocator_t allocator,
                                      const char *file,
                                      neo_position_t *position) {
  neo_ast_node_t node = NULL;
  node = neo_ast_read_statement_empty(allocator, file, position);
  if (!node) {
    node = neo_ast_read_statement_block(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_statement_debugger(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_statement_break(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_statement_continue(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_statement_return(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_statement_throw(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_statement_if(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_statement_switch(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_statement_try(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_statement_while(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_statement_do_while(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_statement_for_await_of(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_statement_for_in(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_statement_for_of(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_statement_for(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_statement_labeled(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_declaration_import(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_declaration_export(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_declaration_class(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_declaration_function(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_declaration_variable(allocator, file, position);
  }
  if (!node) {
    node = neo_ast_read_statement_expression(allocator, file, position);
  }
  return node;
}
