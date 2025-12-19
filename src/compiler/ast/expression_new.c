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
#include "core/any.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"

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

static neo_any_t
neo_serialize_ast_expression_new(neo_allocator_t allocator,
                                 neo_ast_expression_new_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_EXPRESSION_NEW"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "name",
              neo_ast_node_serialize(allocator, node->callee));
  neo_any_set(variable, "arguments",
              neo_ast_node_list_serialize(allocator, node->arguments));
  return variable;
}
static void neo_ast_expression_new_write(neo_allocator_t allocator,
                                         neo_write_context_t ctx,
                                         neo_ast_expression_new_t self) {
  neo_list_initialize_t initialize = {true};
  neo_list_t addresses = neo_create_list(allocator, &initialize);
  neo_write_optional_chain(allocator, ctx, self->callee, addresses);
  neo_allocator_free(allocator, addresses);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_ARRAY);
  for (neo_list_node_t it = neo_list_get_first(self->arguments);
       it != neo_list_get_tail(self->arguments); it = neo_list_node_next(it)) {
    neo_ast_node_t argument = neo_list_node_get(it);
    if (argument->type != NEO_NODE_TYPE_EXPRESSION_SPREAD) {
      argument->write(allocator, ctx, argument);
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_APPEND);
    } else {
      argument->write(allocator, ctx, argument);
    }
  }
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_NEW);
  neo_js_program_add_integer(allocator, ctx->program,
                             self->node.location.begin.line);
  neo_js_program_add_integer(allocator, ctx->program,
                             self->node.location.begin.column);
}

static neo_ast_expression_new_t
neo_create_ast_expression_new(neo_allocator_t allocator) {
  neo_ast_expression_new_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_expression_new_t),
                          neo_ast_expression_new_dispose);
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
  neo_ast_node_t error = NULL;
  neo_position_t current = *position;
  neo_ast_expression_new_t node = NULL;
  neo_ast_node_t callee = NULL;
  neo_token_t token = NULL;
  node = neo_create_ast_expression_new(allocator);
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "new")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  node->callee = neo_ast_read_expression_new(allocator, file, &current);
  if (!node->callee) {
    node->callee = neo_ast_read_expression_18(allocator, file, &current);
  }
  if (!node->callee) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  NEO_CHECK_NODE(node->callee, error, onerror);
  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  for (;;) {
    neo_ast_node_t bnode = NULL;
    bnode = neo_ast_read_expression_member(allocator, file, &current);
    if (bnode) {
      NEO_CHECK_NODE(bnode, error, onerror);
      ((neo_ast_expression_member_t)bnode)->host = node->callee;
    }
    if (!bnode) {
      bnode = neo_ast_read_literal_template(allocator, file, &current);
      if (bnode) {
        NEO_CHECK_NODE(bnode, error, onerror);
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

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
  }
  neo_position_t cur = current;
  error = neo_skip_all(allocator, file, &cur);
  if (error) {
    goto onerror;
  }
  if (*cur.offset == '(') {
    current = cur;
    neo_ast_expression_call_t call =
        (neo_ast_expression_call_t)neo_ast_read_expression_call(allocator, file,
                                                                &current);
    if (!call) {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
      goto onerror;
    }
    if (call->node.type == NEO_NODE_TYPE_ERROR) {
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
  return error;
}