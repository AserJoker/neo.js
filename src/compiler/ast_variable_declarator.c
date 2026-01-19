#include "neojs/compiler/ast_variable_declarator.h"
#include "neojs/compiler/asm.h"
#include "neojs/compiler/ast_expression.h"
#include "neojs/compiler/ast_identifier.h"
#include "neojs/compiler/ast_node.h"
#include "neojs/compiler/ast_pattern_array.h"
#include "neojs/compiler/ast_pattern_object.h"
#include "neojs/compiler/program.h"
#include "neojs/core/allocator.h"
#include "neojs/core/any.h"
#include "neojs/core/list.h"
#include "neojs/core/location.h"

static void
neo_ast_variable_declarator_dispose(neo_allocator_t allocator,
                                    neo_ast_variable_declarator_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->initialize);
  neo_allocator_free(allocator, node->node.scope);
}
static void
neo_ast_variable_declarator_resolve_closure(neo_allocator_t allocator,
                                            neo_ast_variable_declarator_t self,
                                            neo_list_t closure) {
  if (self->initialize) {
    self->initialize->resolve_closure(allocator, self->initialize, closure);
  }
  if (self->identifier->type != NEO_NODE_TYPE_IDENTIFIER) {
    self->identifier->resolve_closure(allocator, self->identifier, closure);
  }
}
static void
neo_ast_variable_declarator_write(neo_allocator_t allocator,
                                  neo_write_context_t ctx,
                                  neo_ast_variable_declarator_t self) {
  if (self->initialize) {
    self->initialize->write(allocator, ctx, self->initialize);
  } else {
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_UNDEFINED);
  }
  if (self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
    char *name = neo_location_get(allocator, self->identifier->location);
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
    neo_js_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  } else {
    self->identifier->write(allocator, ctx, self->identifier);
  }
}
static neo_any_t
neo_serialize_ast_variable_declarator(neo_allocator_t allocator,
                                      neo_ast_variable_declarator_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(
      variable, "type",
      neo_create_any_string(allocator, "NEO_NODE_TYPE_VARIABLE_DECLARATOR"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "identifier",
              neo_ast_node_serialize(allocator, node->identifier));
  neo_any_set(variable, "initialize",
              neo_ast_node_serialize(allocator, node->initialize));
  return variable;
}
static neo_ast_variable_declarator_t
neo_create_ast_variable_declarator(neo_allocator_t allocator) {
  neo_ast_variable_declarator_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_variable_declarator_t),
      neo_ast_variable_declarator_dispose);
  node->node.type = NEO_NODE_TYPE_VARIABLE_DECLARATOR;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_variable_declarator;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_variable_declarator_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_variable_declarator_write;
  node->identifier = NULL;
  node->initialize = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_variable_declarator(neo_allocator_t allocator,
                                                const char *file,
                                                neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_node_t error = NULL;
  neo_ast_variable_declarator_t node =
      neo_create_ast_variable_declarator(allocator);
  node->identifier = neo_ast_read_pattern_object(allocator, file, &current);
  if (!node->identifier) {
    node->identifier = neo_ast_read_pattern_array(allocator, file, &current);
  }
  if (!node->identifier) {
    node->identifier = neo_ast_read_identifier(allocator, file, &current);
  }
  if (!node->identifier) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  NEO_CHECK_NODE(node->identifier, error, onerror);
  neo_position_t cur = current;
  error = neo_skip_all(allocator, file, &cur);
  if (error) {
    goto onerror;
  }
  if (*cur.offset == '=') {
    current = cur;
    current.offset++;
    current.column++;

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    node->initialize = neo_ast_read_expression_2(allocator, file, &current);
    if (!node->initialize) {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
      goto onerror;
    }
    NEO_CHECK_NODE(node->initialize, error, onerror);
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return error;
}