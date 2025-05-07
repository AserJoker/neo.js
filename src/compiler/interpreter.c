
#include "compiler/interpreter.h"
static neo_ast_interpreter_node_t
neo_create_interpreter_node(neo_allocator_t allocator) {
  neo_ast_interpreter_node_t node =
      (neo_ast_interpreter_node_t)neo_allocator_alloc(
          allocator, sizeof(struct _neo_ast_interpreter_node_t), NULL);
  node->node.type = NEO_NODE_TYPE_INTERPRETER_DIRECTIVE;
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
    neo_ast_interpreter_node_t node = neo_create_interpreter_node(allocator);
    node->node.location.begin = *position;
    node->node.location.end = current;
    node->node.location.file = file;
    *position = current;
    return &node->node;
  }
  return NULL;
}