#include "compiler/ast/import_specifier.h"
#include "compiler/asm.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/literal_string.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/location.h"
#include <stdio.h>
#include <string.h>

static void neo_ast_import_specifier_dispose(neo_allocator_t allocator,
                                             neo_ast_import_specifier_t node) {
  neo_allocator_free(allocator, node->alias);
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->node.scope);
}
static void neo_ast_import_specifier_write(neo_allocator_t allocator,
                                           neo_write_context_t ctx,
                                           neo_ast_import_specifier_t self) {
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
  neo_js_program_add_integer(allocator, ctx->program, 1);
  char *name = neo_location_get(allocator, self->identifier->location);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
  if (self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
    neo_js_program_add_string(allocator, ctx->program, name);
  } else {
    name[strlen(name) - 1] = 0;
    neo_js_program_add_string(allocator, ctx->program, name + 1);
  }
  neo_allocator_free(allocator, name);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_GET_FIELD);
  neo_ast_node_t identifier = self->alias;
  if (!identifier) {
    identifier = self->identifier;
  }
  name = neo_location_get(allocator, identifier->location);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
  if (identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
    neo_js_program_add_string(allocator, ctx->program, name);
  } else {
    name[strlen(name) - 1] = 0;
    neo_js_program_add_string(allocator, ctx->program, name + 1);
  }
  neo_allocator_free(allocator, name);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
}
static neo_any_t
neo_serialize_ast_import_specifier(neo_allocator_t allocator,
                                   neo_ast_import_specifier_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(
      variable, "type",
      neo_create_any_string(allocator, "NEO_NODE_TYPE_IMPORT_SPECIFIER"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "identifier",
              neo_ast_node_serialize(allocator, node->identifier));
  neo_any_set(variable, "alias",
              neo_ast_node_serialize(allocator, node->alias));
  return variable;
}

static neo_ast_import_specifier_t
neo_create_ast_import_specifier(neo_allocator_t allocator) {
  neo_ast_import_specifier_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_import_specifier_t),
                          neo_ast_import_specifier_dispose);
  node->node.type = NEO_NODE_TYPE_IMPORT_SPECIFIER;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_import_specifier;
  node->node.resolve_closure = neo_ast_node_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_import_specifier_write;
  node->alias = NULL;
  node->identifier = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_import_specifier(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_node_t error = NULL;
  neo_ast_import_specifier_t node = neo_create_ast_import_specifier(allocator);
  node->identifier = neo_ast_read_identifier(allocator, file, &current);
  if (!node->identifier) {
    node->identifier = neo_ast_read_literal_string(allocator, file, &current);
  }
  if (!node->identifier) {
    goto onerror;
  }
  NEO_CHECK_NODE(node->identifier, error, onerror);
  neo_position_t cur = current;
  error = neo_skip_all(allocator, file, &cur);
  if (error) {
    goto onerror;
  }
  token = neo_read_identify_token(allocator, file, &cur);
  if (token && neo_location_is(token->location, "as")) {
    neo_allocator_free(allocator, token);
    current = cur;

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    node->alias = neo_ast_read_identifier(allocator, file, &current);
    if (!node->alias) {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
      goto onerror;
    }
  }
  neo_allocator_free(allocator, token);
  if (node->alias) {
    error = neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                     node->alias, NEO_COMPILE_VARIABLE_CONST);
    if (error) {
      goto onerror;
    };
  } else {
    error =
        neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                                 node->identifier, NEO_COMPILE_VARIABLE_CONST);
    if (error) {
      goto onerror;
    };
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  neo_allocator_free(allocator, token);
  return error;
}