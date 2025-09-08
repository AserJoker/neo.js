#include "compiler/asm.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <string.h>

static void neo_ast_identifier_dispose(neo_allocator_t allocator,
                                       neo_ast_identifier_t node) {
  neo_allocator_free(allocator, node->node.scope);
}
static void neo_ast_identifier_resolve_closure(neo_allocator_t allocator,
                                               neo_ast_identifier_t self,
                                               neo_list_t closure) {
  neo_compile_scope_t scope = neo_compile_scope_get_current();
  char *name = neo_location_get(allocator, self->node.location);
  if (strcmp(name, "arguments") == 0) {
    neo_allocator_free(allocator, name);
    return;
  }
  for (;;) {
    for (neo_list_node_t it = neo_list_get_first(scope->variables);
         it != neo_list_get_tail(scope->variables);
         it = neo_list_node_next(it)) {
      neo_compile_variable_t variable = neo_list_node_get(it);
      char *varname = neo_location_get(allocator, variable->node->location);
      if (strcmp(varname, name) == 0) {
        neo_allocator_free(allocator, varname);
        neo_allocator_free(allocator, name);
        return;
      }
      neo_allocator_free(allocator, varname);
    }
    if (scope->type != NEO_COMPILE_SCOPE_FUNCTION) {
      scope = scope->parent;
    } else {
      break;
    }
    if (!scope) {
      break;
    }
  }
  for (neo_list_node_t it = neo_list_get_first(closure);
       it != neo_list_get_tail(closure); it = neo_list_node_next(it)) {
    neo_ast_node_t node = neo_list_node_get(it);
    char *current = neo_location_get(allocator, node->location);
    if (strcmp(current, name) == 0) {
      neo_allocator_free(allocator, name);
      neo_allocator_free(allocator, current);
      return;
    }
    neo_allocator_free(allocator, current);
  }
  neo_allocator_free(allocator, name);
  neo_list_push(closure, self);
}
static void neo_ast_identifier_write(neo_allocator_t allocator,
                                     neo_write_context_t ctx,
                                     neo_ast_identifier_t self) {
  char *name = neo_location_get(allocator, self->node.location);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_LOAD);
  neo_program_add_string(allocator, ctx->program, name);
  neo_allocator_free(allocator, name);
}
static neo_variable_t neo_serialize_ast_identifier(neo_allocator_t allocator,
                                                   neo_ast_identifier_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, L"type",
      neo_create_variable_string(allocator, L"NEO_NODE_TYPE_IDENTIFIER"));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}
static neo_ast_identifier_t
neo_create_ast_literal_identify(neo_allocator_t allocator) {
  neo_ast_identifier_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_identifier_t),
                          neo_ast_identifier_dispose);
  node->node.type = NEO_NODE_TYPE_IDENTIFIER;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_identifier;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_identifier_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_identifier_write;
  return node;
}

bool neo_is_keyword(neo_location_t location) {
  static const char *keywords[] = {
      "break",    "case",    "catch",      "class", "const",    "continue",
      "debugger", "default", "delete",     "do",    "else",     "export",
      "extends",  "false",   "finally",    "for",   "function", "if",
      "import",   "in",      "instanceof", "new",   "null",     "return",
      "super",    "switch",  "this",       "throw", "true",     "try",
      "typeof",   "var",     "void",       "while", "with",     "let",
      "static",   0};
  for (size_t idx = 0; keywords[idx] != 0; idx++) {
    if (neo_location_is(location, keywords[idx])) {
      return true;
    }
  }
  return false;
}

neo_ast_node_t neo_ast_read_identifier(neo_allocator_t allocator,
                                       const char *file,
                                       neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_identifier_t node = NULL;
  neo_token_t token = TRY(neo_read_identify_token(allocator, file, &current)) {
    goto onerror;
  };
  if (!token) {
    return NULL;
  }
  if (neo_is_keyword(token->location)) {
    neo_allocator_free(allocator, token);
    return NULL;
  }
  if (*token->location.begin.offset == '#') {
    neo_allocator_free(allocator, token);
    return NULL;
  }
  neo_allocator_free(allocator, token);
  node = neo_create_ast_literal_identify(allocator);
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}

neo_ast_node_t neo_ast_read_identifier_compat(neo_allocator_t allocator,
                                              const char *file,
                                              neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_identifier_t node = NULL;
  neo_token_t token = TRY(neo_read_identify_token(allocator, file, &current)) {
    goto onerror;
  };
  if (!token) {
    return NULL;
  }
  if (*token->location.begin.offset == '#') {
    neo_allocator_free(allocator, token);
    return NULL;
  }
  neo_allocator_free(allocator, token);
  node = neo_create_ast_literal_identify(allocator);
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}