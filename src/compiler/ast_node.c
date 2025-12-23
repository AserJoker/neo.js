#include "compiler/ast_node.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/list.h"
#include "core/location.h"
#include "core/unicode.h"
#include <stdarg.h>
#include <string.h>

neo_any_t neo_ast_node_serialize(neo_allocator_t allocator,
                                 neo_ast_node_t node) {
  if (!node) {
    return neo_create_any_nil(allocator);
  }
  return node->serialize(allocator, node);
}
neo_any_t neo_ast_node_list_serialize(neo_allocator_t allocator,
                                      neo_list_t list) {
  return neo_create_any_array(allocator, list,
                              (neo_serialize_fn_t)neo_ast_node_serialize);
}

neo_any_t neo_ast_node_source_serialize(neo_allocator_t allocator,
                                        neo_ast_node_t node) {
  size_t size = node->location.end.offset - node->location.begin.offset;
  char *buf = neo_allocator_alloc(allocator, size * 2 * sizeof(char), NULL);
  char *dst = buf;
  const char *src = node->location.begin.offset;
  while (src != node->location.end.offset) {
    if (*src == '\"') {
      *dst++ = '\\';
      *dst++ = '\"';
      src++;
    } else if (*src == '\n') {
      *dst++ = '\\';
      *dst++ = 'n';
      src++;
    } else if (*src == '\r') {
      *dst++ = '\\';
      *dst++ = 'r';
      src++;
    } else {
      *dst++ = *src++;
    }
  }
  *dst = 0;
  neo_any_t variable = neo_create_any_string(allocator, buf);
  neo_allocator_free(allocator, buf);
  return variable;
}

neo_any_t neo_ast_node_location_serialize(neo_allocator_t allocator,
                                          neo_ast_node_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);

  neo_any_set(variable, "text", neo_ast_node_source_serialize(allocator, node));
  return variable;
}

void neo_ast_node_resolve_closure(neo_allocator_t allocator,
                                  neo_ast_node_t self, neo_list_t closure) {}

bool neo_skip_white_space(neo_allocator_t allocator, const char *file,
                          neo_position_t *position) {
  if (*position->offset == '\0') {
    return false;
  }
  neo_utf8_char chr = neo_utf8_read_char(position->offset);
  if (neo_utf8_char_is(chr, "\ufeff") || *position->offset == 0x9 ||
      *position->offset == 0xb || *position->offset == 0xc ||
      neo_utf8_char_is_space_separator(chr)) {
    position->column += chr.end - chr.begin;
    position->offset = chr.end;
    return true;
  }
  return false;
}

bool neo_skip_line_terminator(neo_allocator_t allocator, const char *file,
                              neo_position_t *position) {
  if (*position->offset == '\0') {
    return false;
  }
  neo_utf8_char chr = neo_utf8_read_char(position->offset);
  if (neo_utf8_char_is(chr, "\u2028") || neo_utf8_char_is(chr, "\u2029") ||
      *position->offset == '\r') {
    position->column = 1;
    position->line++;
    position->offset = chr.end;
    if (*position->offset == '\n') {
      position->offset++;
    }
    return true;
  } else if (*position->offset == '\n') {
    position->column = 1;
    position->line++;
    position->offset++;
    if (*position->offset == '\r') {
      position->offset++;
    }
    return true;
  }
  return false;
}

neo_ast_node_t neo_skip_comment(neo_allocator_t allocator, const char *file,
                                neo_position_t *position) {
  neo_token_t token = neo_read_comment_token(allocator, file, position);
  if (!token) {
    token = neo_read_multiline_comment_token(allocator, file, position);
    if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
      neo_ast_node_t error = neo_create_error_node(allocator, NULL);
      error->error = token->error;
      token->error = NULL;
      neo_allocator_free(allocator, token);
      return error;
    };
  }
  neo_allocator_free(allocator, token);
  return NULL;
}
static void neo_ast_error_node_dispose(neo_allocator_t allocator,
                                       neo_ast_node_t self) {
  neo_allocator_free(allocator, self->error);
}

neo_ast_node_t neo_create_error_node(neo_allocator_t allocator,
                                     const char *message, ...) {
  neo_ast_node_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_node_t), neo_ast_error_node_dispose);
  node->error = NULL;
  if (message) {
    node->error = neo_allocator_alloc(allocator, 4096, NULL);
    va_list args;
    va_start(args, message);
    vsnprintf(node->error, 4096, message, args);
    va_end(args);
  }
  return node;
}
