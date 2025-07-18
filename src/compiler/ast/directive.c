#include "compiler/ast/directive.h"
#include "compiler/asm.h"
#include "compiler/ast/literal_string.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"

static void neo_ast_directive_dispose(neo_allocator_t allocator,
                                      neo_ast_directive_t node) {
  neo_allocator_free(allocator, node->node.scope);
}

static void neo_ast_directive_write(neo_allocator_t allocator,
                                    neo_write_context_t ctx,
                                    neo_ast_directive_t self) {
  wchar_t *direcitve = neo_location_get(allocator, self->node.location);
  direcitve[wcslen(direcitve) - 1] = '\0';
  neo_program_add_code(allocator, ctx->program, NEO_ASM_DIRECTIVE);
  neo_program_add_string(allocator, ctx->program, direcitve + 1);
  neo_allocator_free(allocator, direcitve);
}

static neo_variable_t neo_serialize_ast_directive(neo_allocator_t allocator,
                                                  neo_ast_directive_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, L"type",
      neo_create_variable_string(allocator, L"NEO_NODE_TYPE_DIRECTIVE"));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_directive_t neo_create_ast_directive(neo_allocator_t allocator) {
  neo_ast_directive_t node = neo_allocator_alloc2(allocator, neo_ast_directive);
  node->node.type = NEO_NODE_TYPE_DIRECTIVE;
  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_directive;
  node->node.resolve_closure = neo_ast_node_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_directive_write;
  return node;
}

neo_ast_node_t neo_ast_read_directive(neo_allocator_t allocator,
                                      const wchar_t *file,
                                      neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_directive_t node = NULL;
  neo_ast_node_t token =
      TRY(neo_ast_read_literal_string(allocator, file, &current)) {
    goto onerror;
  };
  if (!token) {
    neo_allocator_free(allocator, token);
    return NULL;
  }
  neo_allocator_free(allocator, token);
  neo_position_t backup = current;
  uint32_t line = current.line;
  SKIP_ALL(allocator, file, &current, onerror);
  if (current.line == line) {
    if (*current.offset && *current.offset != ';') {
      return NULL;
    }
  }
  node = neo_create_ast_directive(allocator);
  node->node.location.begin = *position;
  node->node.location.end = backup;
  node->node.location.file = file;
  *position = backup;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}