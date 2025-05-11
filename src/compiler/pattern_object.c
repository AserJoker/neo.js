#include "compiler/pattern_object.h"
#include "compiler/node.h"
#include "compiler/pattern_object_item.h"
#include "compiler/pattern_rest.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/position.h"
static void neo_ast_pattern_object_dispose(neo_allocator_t allocator,
                                           neo_ast_pattern_object_t self) {
  neo_allocator_free(allocator, self->items);
}

static neo_ast_pattern_object_t
neo_create_ast_pattern_object(neo_allocator_t allocator) {
  neo_ast_pattern_object_t node =
      neo_allocator_alloc2(allocator, neo_ast_pattern_object);
  neo_list_initialize_t initialize = {true};
  node->items = neo_create_list(allocator, &initialize);
  node->node.type = NEO_NODE_TYPE_PATTERN_OBJECT;
  return node;
}

neo_ast_node_t neo_ast_read_pattern_object(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position) {
  neo_ast_pattern_object_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  if (*current.offset != '{') {
    return NULL;
  }
  current.offset++;
  current.column++;
  node = neo_create_ast_pattern_object(allocator);
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '}') {
    for (;;) {
      neo_ast_node_t item =
          TRY(neo_ast_read_pattern_object_item(allocator, file, &current)) {
        goto onerror;
      };
      if (!item) {
        item = TRY(neo_ast_read_pattern_rest(allocator, file, &current)) {
          goto onerror;
        }
      }
      if (!item) {
        goto onerror;
      }
      neo_list_push(node->items, item);
      SKIP_ALL(allocator, file, &current, onerror);
      if (*current.offset == ',') {
        current.offset++;
        current.column++;
        SKIP_ALL(allocator, file, &current, onerror);
      } else if (*current.offset == '}') {
        break;
      } else {
        goto onerror;
      }
    }
  }
  if (*current.offset != '}') {
    goto onerror;
  }
  current.offset++;
  current.column++;
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