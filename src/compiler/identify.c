#include "compiler/identifier.h"
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"

static void neo_ast_identifier_dispose(neo_allocator_t allocator,
                                       neo_ast_identifier_t node) {
  neo_allocator_free(allocator, node->node.scope);
}
static neo_variable_t neo_serialize_ast_identifier(neo_allocator_t allocator,
                                                   neo_ast_identifier_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_IDENTIFIER"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  return variable;
}
static neo_ast_identifier_t
neo_create_ast_literal_identify(neo_allocator_t allocator) {
  neo_ast_identifier_t node =
      neo_allocator_alloc2(allocator, neo_ast_identifier);
  node->node.type = NEO_NODE_TYPE_IDENTIFIER;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_identifier;
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