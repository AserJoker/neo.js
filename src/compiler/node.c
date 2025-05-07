#include "compiler/node.h"
#include "compiler/token.h"
#include "core/error.h"

bool noix_skip_white_space(noix_allocator_t allocator, const char *file,
                           noix_position_t *position) {
  if (*position->offset == '\0') {
    return false;
  }
  noix_utf8_char chr = noix_utf8_read_char(position->offset);
  if (noix_utf8_char_is(chr, "\ufeff") || *position->offset == 0x9 ||
      *position->offset == 0xb || *position->offset == 0xc ||
      noix_utf8_char_is_space_separator(chr)) {
    position->column += chr.end - chr.begin;
    position->offset = chr.end;
    return true;
  }
  return false;
}

bool noix_skip_line_terminator(noix_allocator_t allocator, const char *file,
                               noix_position_t *position) {
  if (*position->offset == '\0') {
    return false;
  }
  noix_utf8_char chr = noix_utf8_read_char(position->offset);
  if (noix_utf8_char_is(chr, "\u2028") || noix_utf8_char_is(chr, "\u2029") ||
      *position->offset == '\r') {
    position->column = 1;
    position->line++;
    position->offset = chr.end;
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

bool noix_skip_comment(noix_allocator_t allocator, const char *file,
                       noix_position_t *position) {
  noix_token_t token = TRY(noix_read_comment_token(allocator, file, position)) {
    return false;
  };

  if (!token) {
    token = TRY(noix_read_multiline_comment_token(allocator, file, position)) {
      return false;
    };
  }
  if (!token) {
    return false;
  } else {
    noix_allocator_free(allocator, token);
    return true;
  }
}
