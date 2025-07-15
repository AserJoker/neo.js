#include "compiler/ast/declaration_class.h"
#include "compiler/asm.h"
#include "compiler/ast/expression_class.h"
#include "compiler/ast/node.h"
#include "core/allocator.h"
#include "core/variable.h"
static void
neo_ast_declaration_class_dispose(neo_allocator_t allocator,
                                  neo_ast_declaration_class_t node) {
  neo_allocator_free(allocator, node->declaration);
  neo_allocator_free(allocator, node->node.scope);
}

static void neo_ast_declaration_class_write(neo_allocator_t allocator,
                                            neo_write_context_t ctx,
                                            neo_ast_declaration_class_t self) {
  TRY(self->declaration->write(allocator, ctx, self->declaration)) { return; }
  neo_ast_expression_class_t clazz =
      (neo_ast_expression_class_t)self->declaration;
      wchar_t *name = neo_location_get(allocator, clazz->name->location);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
  neo_program_add_string(allocator, ctx->program, name);
  neo_allocator_free(allocator, name);
}

static void
neo_ast_declaration_class_resolve_closure(neo_allocator_t allocator,
                                          neo_ast_declaration_class_t self,
                                          neo_list_t closure) {
  self->declaration->resolve_closure(allocator, self->declaration, closure);
}

static neo_variable_t
neo_serialize_ast_declaration_class(neo_allocator_t allocator,
                                    neo_ast_declaration_class_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, L"type",
      neo_create_variable_string(allocator, L"NEO_NODE_TYPE_DECLARATION_CLASS"));
  neo_variable_set(variable, L"declaration",
                   neo_ast_node_serialize(allocator, node->declaration));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_declaration_class_t
neo_create_ast_declaration_class(neo_allocator_t allocator) {
  neo_ast_declaration_class_t node =
      neo_allocator_alloc2(allocator, neo_ast_declaration_class);
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_declaration_class;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_declaration_class_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_declaration_class_write;
  node->node.type = NEO_NODE_TYPE_DECLARATION_CLASS;
  node->declaration = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_declaration_class(neo_allocator_t allocator,
                                              const wchar_t *file,
                                              neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_class_t declaration = NULL;
  neo_ast_declaration_class_t node =
      neo_create_ast_declaration_class(allocator);
  declaration = (neo_ast_expression_class_t)neo_ast_read_expression_class(
      allocator, file, &current);
  if (!declaration) {
    goto onerror;
  }
  node->declaration = &declaration->node;
  if (!declaration->name) {
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  TRY(neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                               node->declaration, NEO_COMPILE_VARIABLE_LET)) {
    goto onerror;
  };
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}