#include "compiler/ast/variable_declarator.h"
#include "compiler/asm.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/node.h"
#include "compiler/ast/pattern_array.h"
#include "compiler/ast/pattern_object.h"
#include "compiler/program.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/location.h"
#include "core/variable.h"
#include <stdio.h>
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
    TRY(self->initialize->write(allocator, ctx, self->initialize)) { return; }
  } else {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_UNDEFINED);
  }
  if (self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
    char *name = neo_location_get(allocator, self->identifier->location);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  } else {
    TRY(self->identifier->write(allocator, ctx, self->identifier)) { return; }
  }
}
static neo_variable_t
neo_serialize_ast_variable_declarator(neo_allocator_t allocator,
                                      neo_ast_variable_declarator_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_VARIABLE_DECLARATOR"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  neo_variable_set(variable, "initialize",
                   neo_ast_node_serialize(allocator, node->initialize));
  return variable;
}
static neo_ast_variable_declarator_t
neo_create_ast_variable_declarator(neo_allocator_t allocator) {
  neo_ast_variable_declarator_t node =
      neo_allocator_alloc2(allocator, neo_ast_variable_declarator);
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
  neo_ast_variable_declarator_t node =
      neo_create_ast_variable_declarator(allocator);
  node->identifier =
      TRY(neo_ast_read_pattern_object(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_pattern_array(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    node->identifier = TRY(neo_ast_read_identifier(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    THROW("Invalid or unexpected token \n  at _.compile(%s:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  neo_position_t cur = current;
  SKIP_ALL(allocator, file, &cur, onerror);
  if (*cur.offset == '=') {
    current = cur;
    current.offset++;
    current.column++;
    SKIP_ALL(allocator, file, &current, onerror);
    node->initialize =
        TRY(neo_ast_read_expression_2(allocator, file, &current)) {
      goto onerror;
    };
    if (!node->initialize) {
      THROW("Invalid or unexpected token \n  at _.compile(%s:%d:%d)", file,
            current.line, current.column);
      goto onerror;
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