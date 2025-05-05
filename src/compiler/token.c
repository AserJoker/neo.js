#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/position.h"
#include "core/unicode.h"
#include <stdio.h>

noix_token_t noix_read_string_token(noix_allocator_t allocator,
                                    const char *file,
                                    noix_position_t *position) {
  noix_position_t current = *position;
  if (*current.offset != '\'' && *current.offset != '\"') {
    return NULL;
  }
  current.column++;
  current.offset++;
  while (true) {
    noix_utf8_char chr = noix_utf8_read_char(current.offset);
    if (*chr.begin == 0xa || *chr.begin == 0xd || *chr.begin == '\0' ||
        noix_utf8_char_is(chr, "\u2028") || noix_utf8_char_is(chr, "\u2029")) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      return NULL;
    }
    if (noix_utf8_char_is(chr, "\\")) {
      current.offset++;
      current.column++;
      if (*current.offset == 'u') {
        current.offset++;
        current.column++;
        if (*current.offset == '{') {
          current.offset++;
          current.column++;
          if (*current.offset >= '0' && *current.offset <= '9' ||
              (*current.offset >= 'a' && *current.offset <= 'f') ||
              (*current.offset >= 'A' && *current.offset <= 'F')) {
            while (*current.offset >= '0' && *current.offset <= '9' ||
                   (*current.offset >= 'a' && *current.offset <= 'f') ||
                   (*current.offset >= 'A' && *current.offset <= 'F')) {
              current.offset++;
              current.column++;
            }
            if (*current.offset == '}') {
              current.offset++;
              current.column++;
            } else {
              THROW("SyntaxError",
                    "Invalid hexadecimal escape sequence \n  at %s:%d:%d", file,
                    current.line, current.column);
            }
          } else {
            THROW("SyntaxError",
                  "Invalid hexadecimal escape sequence \n  at %s:%d:%d", file,
                  current.line, current.column);
          }
        } else {
          for (int8_t i = 0; i < 4; i++) {
            if ((*current.offset >= '0' && *current.offset <= '9') ||
                (*current.offset >= 'a' && *current.offset <= 'f') ||
                (*current.offset >= 'A' && *current.offset <= 'F')) {
              current.offset++;
              current.column++;
            } else {
              THROW("SyntaxError",
                    "Invalid hexadecimal escape sequence \n  at %s:%d:%d", file,
                    current.line, current.column);
            }
          }
        }
        continue;
      } else if (*current.offset == 'x') {
        current.offset++;
        current.column++;
        for (int8_t i = 0; i < 2; i++) {
          if ((*current.offset >= '0' && *current.offset <= '9') ||
              (*current.offset >= 'a' && *current.offset <= 'f') ||
              (*current.offset >= 'A' && *current.offset <= 'F')) {
            current.offset++;
            current.column++;
          } else {
            THROW("SyntaxError",
                  "Invalid hexadecimal escape sequence \n  at %s:%d:%d", file,
                  current.line, current.column);
          }
        }
        continue;
      }
    }
    if (*chr.begin == *position->offset) {
      current.column++;
      current.offset++;
      break;
    }
    current.offset = chr.end;
    current.column += chr.end - chr.begin;
  }
  noix_token_t token = (noix_token_t)noix_allocator_alloc(
      allocator, sizeof(struct _noix_token_t), NULL);
  token->type = NOIX_TOKEN_TYPE_STRING;
  token->position.begin = *position;
  token->position.end = current;
  token->position.file = file;
  token->type = NOIX_TOKEN_TYPE_STRING;
  *position = current;
  return token;
}