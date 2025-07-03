#include "compiler/ast/expression_new.h"
#include "compiler/asm.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/expression_call.h"
#include "compiler/ast/expression_member.h"
#include "compiler/ast/literal_template.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_expression_new_dispose(neo_allocator_t allocator,
                                           neo_ast_expression_new_t node) {
  neo_allocator_free(allocator, node->arguments);
  neo_allocator_free(allocator, node->callee);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_expression_new_resolve_closure(neo_allocator_t allocator,
                                       neo_ast_expression_new_t self,
                                       neo_list_t closure) {
  self->callee->resolve_closure(allocator, self->callee, closure);
  for (neo_list_node_t it = neo_list_get_first(self->arguments);
       it != neo_list_get_tail(self->arguments); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
}

static neo_variable_t
neo_serialize_ast_expression_new(neo_allocator_t allocator,
                                 neo_ast_expression_new_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_EXPRESSION_NEW"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "name",
                   neo_ast_node_serialize(allocator, node->callee));
  neo_variable_set(variable, "arguments",
                   neo_ast_node_list_serialize(allocator, node->arguments));
  return variable;
}
static void neo_ast_expression_new_write(neo_allocator_t allocator,
                                         neo_write_context_t ctx,
                                         neo_ast_expression_new_t self) {
  neo_list_initialize_t initialize = {true};
  neo_list_t addresses = neo_create_list(allocator, &initialize);
  TRY(neo_write_optional_chain(allocator, ctx, self->callee, addresses)) {
    neo_allocator_free(allocator, addresses);
    return;
  }
  if (neo_list_get_size(addresses) != 0) {
    neo_allocator_free(allocator, addresses);
    THROW("Invalid or unexpected token \n  at %s:%d:%d", ctx->program->file,
          self->callee->location.begin.line,
          self->callee->location.begin.column);
    return;
  }
  neo_allocator_free(allocator, addresses);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_ARRAY);
  neo_program_add_number(allocator, ctx->program, 0);
  for (neo_list_node_t it = neo_list_get_first(self->arguments);
       it != neo_list_get_tail(self->arguments); it = neo_list_node_next(it)) {
    neo_ast_node_t argument = neo_list_node_get(it);
    if (argument->type != NEO_NODE_TYPE_EXPRESSION_SPREAD) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
      neo_program_add_integer(allocator, ctx->program, 1);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
      neo_program_add_string(allocator, ctx->program, "length");
      neo_program_add_code(allocator, ctx->program, NEO_ASM_GET_FIELD);
      TRY(argument->write(allocator, ctx, argument)) { return; }
      neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
    } else {
      TRY(argument->write(allocator, ctx, argument)) { return; }
    }
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_NEW);
  neo_program_add_integer(allocator, ctx->program,
                          self->node.location.begin.line);
  neo_program_add_integer(allocator, ctx->program,
                          self->node.location.begin.column);
}

static neo_ast_expression_new_t
neo_create_ast_expression_new(neo_allocator_t allocator) {
  neo_ast_expression_new_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_new);
  node->callee = NULL;
  neo_list_initialize_t initialize = {true};
  node->arguments = neo_create_list(allocator, &initialize);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_NEW;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_expression_new;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_expression_new_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_expression_new_write;
  return node;
}

neo_ast_node_t neo_ast_read_expression_new(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_new_t node = NULL;
  neo_ast_node_t callee = NULL;
  neo_token_t token = NULL;
  node = neo_create_ast_expression_new(allocator);
  token = TRY(neo_read_identify_token(allocator, file, &current)) {
    goto onerror;
  };
  if (!token || !neo_location_is(token->location, "new")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->callee = TRY(neo_ast_read_expression_new(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->callee) {
    node->callee = TRY(neo_ast_read_expression_18(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->callee) {
    THROW("Invalid or unexpected token \n  at %s:%d:%d", file, current.line,
          current.column);
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  for (;;) {
    neo_ast_node_t bnode = NULL;
    bnode = TRY(neo_ast_read_expression_member(allocator, file, &current)) {
      goto onerror;
    };
    if (bnode) {
      ((neo_ast_expression_member_t)bnode)->host = node->callee;
    }
    if (!bnode) {
      bnode = TRY(neo_ast_read_literal_template(allocator, file, &current)) {
        goto onerror;
      };
      if (bnode) {
        ((neo_ast_literal_template_t)bnode)->tag = node->callee;
      }
    }
    if (!bnode) {
      break;
    }
    node->callee = bnode;
    node->callee->location.begin = *position;
    node->callee->location.end = current;
    node->callee->location.file = file;
    SKIP_ALL(allocator, file, &current, onerror);
  }
  neo_position_t cur = current;
  SKIP_ALL(allocator, file, &cur, onerror);
  if (*cur.offset == '(') {
    current = cur;
    neo_ast_expression_call_t call = (neo_ast_expression_call_t)TRY(
        neo_ast_read_expression_call(allocator, file, &current)) {
      goto onerror;
    };
    if (!call) {
      THROW("Invalid or unexpected token \n  at %s:%d:%d", file, current.line,
            current.column);
      goto onerror;
    }
    neo_allocator_free(allocator, node->arguments);
    node->arguments = call->arguments;
    call->arguments = NULL;
    neo_allocator_free(allocator, call);
  }
  node->node.location.file = file;
  node->node.location.begin = *position;
  node->node.location.end = current;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, callee);
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}