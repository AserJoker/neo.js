
#include "compiler/interpreter.h"
static noix_interpreter_node_t
noix_create_interpreter_node(noix_allocator_t allocator) {
  noix_interpreter_node_t node = (noix_interpreter_node_t)noix_allocator_alloc(
      allocator, sizeof(struct _noix_interpreter_node_t), NULL);
  node->node.type = NOIX_NODE_TYPE_INTERPRETER_DIRECTIVE;
  return node;
}

noix_ast_node_t noix_read_interpreter(noix_allocator_t allocator,
                                      const char *file,
                                      noix_position_t *position) {
  noix_position_t current = *position;
  if (*current.offset == '#' && *(current.offset + 1) == '!') {
    current.offset += 2;
    current.column += 2;
    for (;;) {
      noix_utf8_char chr = noix_utf8_read_char(current.offset);
      if (*current.offset == '\0' || *current.offset == 0xa ||
          *current.offset == 0xd || noix_utf8_char_is(chr, "\u2028") ||
          *current.offset == 0xd || noix_utf8_char_is(chr, "\u2029")) {
        break;
      }
      current.column = chr.end - chr.begin;
      current.offset = chr.end;
    }
    noix_interpreter_node_t node = noix_create_interpreter_node(allocator);
    node->node.location.begin = *position;
    node->node.location.end = current;
    node->node.location.file = file;
    *position = current;
    return &node->node;
  }
  return NULL;
}