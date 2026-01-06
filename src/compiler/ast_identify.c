#include "compiler/asm.h"
#include "compiler/ast_identifier.h"
#include "compiler/ast_node.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
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
  if (scope) {
    scope = scope->parent;
    while (scope) {
      for (neo_list_node_t it = neo_list_get_first(scope->variables);
           it != neo_list_get_tail(scope->variables);
           it = neo_list_node_next(it)) {
        neo_compile_variable_t variable = neo_list_node_get(it);
        char *varname = neo_location_get(allocator, variable->node->location);
        if (strcmp(varname, name) == 0) {
          neo_allocator_free(allocator, varname);
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
          return;
        }
        neo_allocator_free(allocator, varname);
      }
      scope = scope->parent;
    }
  }
  neo_allocator_free(allocator, name);
}
static void neo_ast_identifier_write(neo_allocator_t allocator,
                                     neo_write_context_t ctx,
                                     neo_ast_identifier_t self) {
  char *name = neo_location_get(allocator, self->node.location);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_LOAD);
  neo_js_program_add_string(allocator, ctx->program, name);
  neo_allocator_free(allocator, name);
}
static neo_any_t neo_serialize_ast_identifier(neo_allocator_t allocator,
                                              neo_ast_identifier_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_IDENTIFIER"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
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
  neo_ast_node_t error = NULL;
  neo_token_t token = neo_read_identify_token(allocator, file, &current);
  if (!token) {
    return NULL;
  }
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
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
  return error;
}

neo_ast_node_t neo_ast_read_identifier_compat(neo_allocator_t allocator,
                                              const char *file,
                                              neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_identifier_t node = NULL;
  neo_token_t token = neo_read_identify_token(allocator, file, &current);
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