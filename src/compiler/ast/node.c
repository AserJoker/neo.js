#include "compiler/ast/node.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/unicode.h"
#include "core/variable.h"
#include <string.h>
#include <wchar.h>

neo_variable_t neo_ast_node_serialize(neo_allocator_t allocator,
                                      neo_ast_node_t node) {
  if (!node) {
    return neo_create_variable_nil(allocator);
  }
  return node->serialize(allocator, node);
}
neo_variable_t neo_ast_node_list_serialize(neo_allocator_t allocator,
                                           neo_list_t list) {
  return neo_create_variable_array(allocator, list,
                                   (neo_serialize_fn_t)neo_ast_node_serialize);
}

neo_variable_t neo_ast_node_source_serialize(neo_allocator_t allocator,
                                             neo_ast_node_t node) {
  size_t size = node->location.end.offset - node->location.begin.offset;
  wchar_t *buf =
      neo_allocator_alloc(allocator, size * 2 * sizeof(wchar_t), NULL);
  wchar_t *dst = buf;
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
  neo_variable_t variable = neo_create_variable_string(allocator, buf);
  neo_allocator_free(allocator, buf);
  return variable;
}

neo_variable_t neo_ast_node_location_serialize(neo_allocator_t allocator,
                                               neo_ast_node_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);

  neo_variable_set(variable, L"text",
                   neo_ast_node_source_serialize(allocator, node));
  return variable;
}

void neo_ast_node_resolve_closure(neo_allocator_t allocator,
                                  neo_ast_node_t self, neo_list_t closure) {}

bool neo_skip_white_space(neo_allocator_t allocator, const wchar_t *file,
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

bool neo_skip_line_terminator(neo_allocator_t allocator, const wchar_t *file,
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

bool neo_skip_comment(neo_allocator_t allocator, const wchar_t *file,
                      neo_position_t *position) {
  neo_token_t token = TRY(neo_read_comment_token(allocator, file, position)) {
    return false;
  };

  if (!token) {
    token = TRY(neo_read_multiline_comment_token(allocator, file, position)) {
      return false;
    };
  }
  if (!token) {
    return false;
  } else {
    neo_allocator_free(allocator, token);
    return true;
  }
}
