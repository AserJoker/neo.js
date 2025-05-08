#include "compiler/node.h"
#include "compiler/token.h"
#include "core/error.h"
#include "core/unicode.h"

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

bool neo_skip_comment(neo_allocator_t allocator, const char *file,
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
