#include "compiler/ast/declaration_function.h"
#include "compiler/asm.h"
#include "compiler/ast/expression_function.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/location.h"
#include "core/variable.h"
#include <stdio.h>
static void
neo_ast_declaration_function_dispose(neo_allocator_t allocator,
                                     neo_ast_declaration_function_t node) {
  neo_allocator_free(allocator, node->declaration);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_declaration_function_write(neo_allocator_t allocator,
                                   neo_write_context_t ctx,
                                   neo_ast_declaration_function_t self) {
  neo_ast_expression_function_t function =
      (neo_ast_expression_function_t)self->declaration;
  char *name = neo_location_get(allocator, function->name->location);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_LOAD);
  neo_program_add_string(allocator, ctx->program, name);
  neo_allocator_free(allocator, name);
  for (neo_list_node_t it = neo_list_get_first(function->closure);
       it != neo_list_get_tail(function->closure);
       it = neo_list_node_next(it)) {
    neo_ast_node_t node = neo_list_node_get(it);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_CLOSURE);
    char *name = neo_location_get(allocator, node->location);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
}
static void neo_ast_declaration_function_resolve_closure(
    neo_allocator_t allocator, neo_ast_declaration_function_t node,
    neo_list_t closure) {
  node->declaration->resolve_closure(allocator, node->declaration, closure);
}

static neo_variable_t
neo_serialize_ast_declaration_function(neo_allocator_t allocator,
                                       neo_ast_declaration_function_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_DECLARATION_FUNCTION"));
  neo_variable_set(variable, "declaration",
                   neo_ast_node_serialize(allocator, node->declaration));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_declaration_function_t
neo_create_ast_declaration_function(neo_allocator_t allocator) {
  neo_ast_declaration_function_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_declaration_function_t),
      neo_ast_declaration_function_dispose);
  node->node.type = NEO_NODE_TYPE_DECLARATION_FUNCTION;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_declaration_function;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_declaration_function_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_declaration_function_write;
  node->declaration = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_declaration_function(neo_allocator_t allocator,
                                                 const char *file,
                                                 neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_function_t declaration = NULL;
  neo_ast_declaration_function_t node =
      neo_create_ast_declaration_function(allocator);
  declaration = (neo_ast_expression_function_t)neo_ast_read_expression_function(
      allocator, file, &current);
  if (!declaration) {
    goto onerror;
  }
  node->declaration = &declaration->node;
  if (!declaration->name) {
    THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  TRY(neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                               node->declaration,
                               NEO_COMPILE_VARIABLE_FUNCTION)) {
    goto onerror;
  };
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}