
#include "compiler/ast/interpreter.h"
#include "core/unicode.h"
#include "core/variable.h"
static void neo_ast_interpreter_dispose(neo_allocator_t allocator,
                                        neo_ast_interpreter_t node) {
  neo_allocator_free(allocator, node->node.scope);
};
static neo_variable_t
neo_serialize_ast_interpreter(neo_allocator_t allocator,
                              neo_ast_interpreter_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_INTERPRETER_DIRECTIVE"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_interpreter_t
neo_create_interpreter_node(neo_allocator_t allocator) {
  neo_ast_interpreter_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_interpreter_t),
                          neo_ast_interpreter_dispose);
  node->node.type = NEO_NODE_TYPE_INTERPRETER_DIRECTIVE;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_interpreter;
  node->node.resolve_closure = neo_ast_node_resolve_closure;
  node->node.write = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_interpreter(neo_allocator_t allocator,
                                        const char *file,
                                        neo_position_t *position) {
  neo_position_t current = *position;
  if (*current.offset == '#' && *(current.offset + 1) == '!') {
    current.offset += 2;
    current.column += 2;
    for (;;) {
      neo_utf8_char chr = neo_utf8_read_char(current.offset);
      if (*current.offset == '\0' || *current.offset == 0xa ||
          *current.offset == 0xd || neo_utf8_char_is(chr, "\u2028") ||
          *current.offset == 0xd || neo_utf8_char_is(chr, "\u2029")) {
        break;
      }
      current.column = chr.end - chr.begin;
      current.offset = chr.end;
    }
    neo_ast_interpreter_t node = neo_create_interpreter_node(allocator);
    node->node.location.begin = *position;
    node->node.location.end = current;
    node->node.location.file = file;
    *position = current;
    return &node->node;
  }
  return NULL;
}