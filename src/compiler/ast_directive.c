#include "neojs/compiler/ast_directive.h"
#include "neojs/compiler/asm.h"
#include "neojs/compiler/ast_literal_string.h"
#include "neojs/compiler/ast_node.h"
#include "neojs/compiler/program.h"
#include "neojs/core/allocator.h"
#include "neojs/core/any.h"
#include "neojs/core/location.h"
#include "neojs/core/position.h"
#include <string.h>

static void neo_ast_directive_dispose(neo_allocator_t allocator,
                                      neo_ast_directive_t node) {
  neo_allocator_free(allocator, node->node.scope);
}

static void neo_ast_directive_write(neo_allocator_t allocator,
                                    neo_write_context_t ctx,
                                    neo_ast_directive_t self) {
  char *direcitve = neo_location_get(allocator, self->node.location);
  direcitve[strlen(direcitve) - 1] = '\0';
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_DIRECTIVE);
  neo_js_program_add_string(allocator, ctx->program, direcitve + 1);
  neo_allocator_free(allocator, direcitve);
}

static neo_any_t neo_serialize_ast_directive(neo_allocator_t allocator,
                                             neo_ast_directive_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_DIRECTIVE"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_directive_t neo_create_ast_directive(neo_allocator_t allocator) {
  neo_ast_directive_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_directive_t),
                          neo_ast_directive_dispose);
  node->node.type = NEO_NODE_TYPE_DIRECTIVE;
  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_directive;
  node->node.resolve_closure = neo_ast_node_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_directive_write;
  return node;
}

neo_ast_node_t neo_ast_read_directive(neo_allocator_t allocator,
                                      const char *file,
                                      neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_directive_t node = NULL;
  neo_ast_node_t error = NULL;
  neo_ast_node_t token = neo_ast_read_literal_string(allocator, file, &current);
  if (!token) {
    neo_allocator_free(allocator, token);
    return NULL;
  }
  if (token->type == NEO_NODE_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    neo_allocator_free(allocator, token);
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  neo_position_t backup = current;
  uint32_t line = current.line;

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
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
  return error;
}