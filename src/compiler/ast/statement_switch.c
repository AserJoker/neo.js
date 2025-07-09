#include "compiler/ast/statement_switch.h"
#include "compiler/asm.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/node.h"
#include "compiler/ast/switch_case.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/map.h"
#include "core/position.h"
#include "core/variable.h"
#include <stddef.h>
#include <stdio.h>

static void neo_ast_statement_switch_dispose(neo_allocator_t allocator,
                                             neo_ast_statement_switch_t node) {
  neo_allocator_free(allocator, node->cases);
  neo_allocator_free(allocator, node->condition);
  neo_allocator_free(allocator, node->node.scope);
}
static void
neo_ast_statement_switch_resolve_closure(neo_allocator_t allocator,
                                         neo_ast_statement_switch_t self,
                                         neo_list_t closure) {
  neo_compile_scope_t scope = neo_compile_scope_set(self->node.scope);
  self->condition->resolve_closure(allocator, self->condition, closure);
  for (neo_list_node_t it = neo_list_get_first(self->cases);
       it != neo_list_get_tail(self->cases); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
  neo_compile_scope_pop(self->node.scope);
}

static void neo_ast_statement_switch_write(neo_allocator_t allocator,
                                           neo_write_context_t ctx,
                                           neo_ast_statement_switch_t self) {
  neo_writer_push_scope(allocator, ctx, self->node.scope);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_BREAK_LABEL);
  neo_program_add_string(allocator, ctx->program, "");
  size_t labeladdr = neo_buffer_get_size(ctx->program->codes);
  neo_program_add_address(allocator, ctx->program, 0);
  TRY(self->condition->write(allocator, ctx, self->condition)) { return; }
  neo_ast_switch_case_t def = NULL;
  neo_map_initialize_t initialize;
  initialize.auto_free_key = false;
  initialize.auto_free_value = true;
  initialize.compare = NULL;
  neo_map_t addresses = neo_create_map(allocator, &initialize);
  for (neo_list_node_t it = neo_list_get_first(self->cases);
       it != neo_list_get_tail(self->cases); it = neo_list_node_next(it)) {
    neo_ast_switch_case_t cas = neo_list_node_get(it);
    if (!cas->condition) {
      def = cas;
    } else {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
      neo_program_add_integer(allocator, ctx->program, 1);
      TRY(cas->condition->write(allocator, ctx, cas->condition)) { return; }
      neo_program_add_code(allocator, ctx->program, NEO_ASM_SEQ);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_JTRUE);
      size_t *address = neo_allocator_alloc(allocator, sizeof(size_t), NULL);
      *address = neo_buffer_get_size(ctx->program->codes);
      neo_program_add_address(allocator, ctx->program, 0);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      neo_map_set(addresses, cas, address, NULL);
    }
  }
  size_t endaddr = 0;
  if (def) {
    size_t *address = neo_allocator_alloc(allocator, sizeof(size_t), NULL);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_JMP);
    *address = neo_buffer_get_size(ctx->program->codes);
    neo_program_add_address(allocator, ctx->program, 0);
    neo_map_set(addresses, def, address, NULL);
  } else {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_JMP);
    endaddr = neo_buffer_get_size(ctx->program->codes);
    neo_program_add_address(allocator, ctx->program, 0);
  }
  for (neo_list_node_t it = neo_list_get_first(self->cases);
       it != neo_list_get_tail(self->cases); it = neo_list_node_next(it)) {
    neo_ast_switch_case_t cas = neo_list_node_get(it);
    size_t *address = neo_map_get(addresses, cas, NULL);
    neo_program_set_current(ctx->program, *address);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    for (neo_list_node_t it = neo_list_get_first(cas->body);
         it != neo_list_get_tail(cas->body); it = neo_list_node_next(it)) {
      neo_ast_node_t item = neo_list_node_get(it);
      TRY(item->write(allocator, ctx, item)) { return; }
    }
    if (it != neo_list_get_last(self->cases)) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_UNDEFINED);
    }
  }
  if (endaddr) {
    neo_program_set_current(ctx->program, endaddr);
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  neo_allocator_free(allocator, addresses);
  neo_program_set_current(ctx->program, labeladdr);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP_LABEL);
  neo_writer_pop_scope(allocator, ctx, self->node.scope);
}

static neo_variable_t
neo_serialize_ast_statement_switch(neo_allocator_t allocator,
                                   neo_ast_statement_switch_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_STATEMENT_SWITCH"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "condition",
                   neo_ast_node_serialize(allocator, node->condition));
  neo_variable_set(variable, "cases",
                   neo_ast_node_list_serialize(allocator, node->cases));
  return variable;
}
static neo_ast_statement_switch_t
neo_create_ast_statement_switch(neo_allocator_t allocator) {
  neo_ast_statement_switch_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_switch);
  node->node.type = NEO_NODE_TYPE_STATEMENT_SWITCH;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_statement_switch;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_statement_switch_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_statement_switch_write;
  node->condition = NULL;
  neo_list_initialize_t initialize = {true};
  node->cases = neo_create_list(allocator, &initialize);
  return node;
}

neo_ast_node_t neo_ast_read_statement_switch(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_statement_switch_t node = neo_create_ast_statement_switch(allocator);
  neo_token_t token = NULL;
  neo_compile_scope_t scope = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "switch")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '(') {
    THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  node->condition = TRY(neo_ast_read_expression(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->condition) {
    THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ')') {
    THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '{') {
    THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  scope =
      neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_BLOCK, false, false);
  SKIP_ALL(allocator, file, &current, onerror);
  for (;;) {
    neo_ast_node_t cas =
        TRY(neo_ast_read_switch_case(allocator, file, &current)) {
      goto onerror;
    }
    if (!cas) {
      break;
    }
    neo_list_push(node->cases, cas);
    SKIP_ALL(allocator, file, &current, onerror);
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '}') {
    THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
          current.line, current.column);
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
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}