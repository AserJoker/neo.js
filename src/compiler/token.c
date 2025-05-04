#include "compiler/token.h"
#include "compiler/location.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/unicode.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static inline noix_token_t noix_create_token(noix_allocator_t allocator) {
  noix_token_t token = (noix_token_t)noix_allocator_alloc(
      allocator, sizeof(struct _noix_token_t), NULL);
  return token;
}

static inline bool is_line_terminator(noix_utf8_char chr) {
  return noix_utf8_char_is(chr, "\xa") || noix_utf8_char_is(chr, "\xd") ||
         noix_utf8_char_is(chr, "\u2028") || noix_utf8_char_is(chr, "\u2029");
}

static inline bool is_white_space(noix_utf8_char chr) {
  return noix_utf8_char_is(chr, "\x9") || noix_utf8_char_is(chr, "\xb") ||
         noix_utf8_char_is(chr, "\xc") || noix_utf8_char_is(chr, "\x20") ||
         noix_utf8_char_is(chr, "\xa0") || noix_utf8_char_is(chr, "\ufeff");
}

static void noix_position_next(noix_position_t *position) {
  noix_utf8_char chr = noix_utf8_read_char(position->offset);
  position->offset = chr.end;
  if (*chr.begin == '\n' || *chr.begin == '\r') {
    position->column = 1;
    position->line++;
  } else {
    position->column += chr.end - chr.begin;
  }
}

noix_token_t noix_read_string_token(noix_allocator_t allocator,
                                    noix_position_t *current) {
  noix_position_t start = *current;
  noix_position_t end = *current;
  noix_token_t token = NULL;
  if (*end.offset == '\"' || *end.offset == '\'') {
    end.offset++;
    end.column++;
    while (true) {
      if (*end.offset == '\0' ||
          is_line_terminator(noix_utf8_read_char(end.offset))) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              end.filename, end.line, end.column);
        noix_allocator_free(allocator, token);
        return NULL;
      } else if (*end.offset == *current->offset) {
        end.offset++;
        end.column++;
        break;
      } else if (*end.offset == '\\') {
        end.offset++;
        end.column++;
        noix_utf8_char chr = noix_utf8_read_char(end.offset);
        if (is_line_terminator(chr) || *end.offset == 0) {
          THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
                end.filename, end.line, end.column);
          noix_allocator_free(allocator, token);
          return NULL;
        }
      }
      noix_position_next(&end);
    }
    noix_token_t token = noix_create_token(allocator);
    token->type = NOIX_TOKEN_STRING;
    token->location.start = start;
    token->location.end = end;
    return token;
  }
  return NULL;
}

noix_token_t noix_read_number_token(noix_allocator_t allocator,
                                    noix_position_t *current) {
  noix_position_t start = *current;
  noix_position_t end = *current;
  if (*(end.offset + 1) == 'x' || *(end.offset + 1) == 'X') {
    end.offset += 2;
    end.column += 2;
    if (*end.offset < '0' || *end.offset > '9') {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
            end.filename, end.line, end.column);
      return NULL;
    }
    while (true) {
      if (*end.offset >= '0' && *end.offset <= '9') {
        end.offset++;
        end.column++;
      } else if (*end.offset >= 'a' && *end.offset <= 'f') {
        end.offset++;
        end.column++;
      } else if (*end.offset >= 'A' && *end.offset <= 'F') {
        end.offset++;
        end.column++;
      } else {
        break;
      }
    }
  } else if (*(end.offset + 1) == 'o' || *(end.offset + 1) == 'O') {
    end.offset += 2;
    end.column += 2;
    if (*end.offset < '0' || *end.offset > '7') {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
            end.filename, end.line, end.column);
      return NULL;
    }
    while (true) {
      if (*end.offset >= '0' && *end.offset <= '7') {
        end.offset++;
        end.column++;
      } else {
        break;
      }
    }
  } else if (*end.offset == '.' || (*end.offset >= '0' && *end.offset <= '9')) {
    while (true) {
      if (*end.offset < '0' || *end.offset > '9') {
        break;
      }
      end.offset++;
      end.column++;
    }
    if (*end.offset == '.') {
      end.offset++;
      end.column++;
    }
    while (true) {
      if (*end.offset < '0' || *end.offset > '9') {
        break;
      }
      end.offset++;
      end.column++;
    }
    if (*end.offset == 'e' || *end.offset == 'E') {
      end.offset++;
      end.column++;
      if (*end.offset == '+' || *end.offset == '-') {
        end.offset++;
        end.column++;
      }
      while (true) {
        if (*end.offset < '0' || *end.offset > '9') {
          break;
        }
        end.offset++;
        end.column++;
      }
    }
  } else {
    return NULL;
  }
  noix_token_t token = noix_create_token(allocator);
  token->type = NOIX_TOKEN_NUMBER;
  token->location.start = start;
  token->location.end = end;
  return token;
}

noix_token_t noix_read_symbol_token(noix_allocator_t allocator,
                                    noix_position_t *current) {
  static const char *operators[] = {
      ">>>=", "...", "<<=", ">>>", "===", "!==", "**=", ">>=", "&&=", "?\?=",
      "**",   "==",  "!=",  "<<",  ">>",  "<=",  ">=",  "&&",  "||",  "??",
      "++",   "--",  "+=",  "-=",  "*=",  "/=",  "%=",  "||=", "&=",  "^=",
      "|=",   "=>",  "?.",  "=",   "*",   "/",   "%",   "+",   "-",   "<",
      ">",    "&",   "^",   "|",   ",",   "!",   "~",   "(",   ")",   "[",
      "]",    "{",   "}",   "@",   "#",   ".",   "?",   ":",   ";",   0};
  noix_position_t start = *current;
  noix_position_t end = *current;
  size_t idx = 0;
  while (operators[idx] != 0) {
    size_t offset = 0;
    while (operators[idx][offset] != 0) {
      if (end.offset[offset] != operators[idx][offset]) {
        break;
      }
      offset++;
    }
    if (operators[idx][offset] == 0) {
      break;
    }
    idx++;
  }
  if (operators[idx] == 0) {
    return NULL;
  }
  noix_token_t token = noix_create_token(allocator);
  token->type = NOIX_TOKEN_SYMBOL;
  token->location.start = start;
  token->location.end = end;
  return token;
}

noix_token_t noix_read_identify_token(noix_allocator_t allocator,
                                      noix_position_t *current) {
  noix_position_t start = *current;
  noix_position_t end = *current;
  noix_utf8_char chr = noix_utf8_read_char(end.offset);
  if (*end.offset == '_' || *end.offset == '$' ||
      (*end.offset >= 'a' && *end.offset <= 'z') ||
      (*end.offset >= 'A' && *end.offset <= 'Z') ||
      (chr.end - chr.begin > 1 &&
       (!is_line_terminator(chr) && !is_white_space(chr)))) {
    end.offset = chr.end;
    end.column += chr.end - chr.begin;
    while (true) {
      chr = noix_utf8_read_char(end.offset);
      if (chr.end == chr.begin) {
        break;
      }
      if (*end.offset == '_' || *end.offset == '$' ||
          (*end.offset >= 'a' && *end.offset <= 'z') ||
          (*end.offset >= 'A' && *end.offset <= 'Z') ||
          (*end.offset >= '0' && *end.offset <= '9') ||
          (chr.end - chr.begin > 1 &&
           (!is_line_terminator(chr) && !is_white_space(chr)))) {
        end.offset = chr.end;
        end.column += chr.end - chr.begin;
      } else {
        break;
      }
    }
  } else {
    return NULL;
  }
  noix_token_t token = noix_create_token(allocator);
  token->type = NOIX_TOKEN_IDENTITY;
  token->location.start = start;
  token->location.end = end;
  return token;
}

noix_token_t noix_read_regexp_token(noix_allocator_t allocator,
                                    noix_position_t *current) {
  noix_position_t start = *current;
  noix_position_t end = *current;
  if (*end.offset == '/' && *(end.offset + 1) != '/') {
    end.offset++;
    end.column++;
    while (true) {
      if (*end.offset == '\0' ||
          is_line_terminator(noix_utf8_read_char(end.offset))) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              end.filename, end.line, end.column);
        return NULL;
      }
      if (*end.offset == '[') {
        end.offset++;
        end.column++;
        while (true) {
          if (*end.offset == '\0' ||
              is_line_terminator(noix_utf8_read_char(end.offset))) {
            THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
                  end.filename, end.line, end.column);
            return NULL;
          }
          if (*end.offset == ']') {
            break;
          }
          noix_position_next(&end);
        }
        continue;
      }
      if (*end.offset == '/') {
        break;
      }
      if (*end.offset == '\\') {
        end.offset++;
        end.column++;
      }
      noix_position_next(&end);
    }
    end.offset++;
    end.column++;
    while (*end.offset >= 'a' && *end.offset <= 'z' ||
           *end.offset >= 'A' && *end.offset <= 'Z') {
      end.offset++;
      end.column++;
    }
  } else {
    return NULL;
  }
  noix_token_t token = noix_create_token(allocator);
  token->type = NOIX_TOKEN_REGEXP;
  token->location.start = start;
  token->location.end = end;
  return token;
}

noix_token_t noix_read_white_space_token(noix_allocator_t allocator,
                                         noix_position_t *current) {
  noix_utf8_char chr = noix_utf8_read_char(current->offset);
  if (is_white_space(chr)) {
    noix_token_t token = noix_create_token(allocator);
    token->type = NOIX_TOKEN_WHITE_SPACE;
    token->location.start = *current;
    token->location.end = *current;
    noix_position_next(&token->location.end);
    return token;
  }
  return NULL;
}

noix_token_t noix_read_line_terminator_token(noix_allocator_t allocator,
                                             noix_position_t *current) {
  noix_utf8_char chr = noix_utf8_read_char(current->offset);
  if (is_line_terminator(chr)) {
    noix_token_t token = noix_create_token(allocator);
    token->type = NOIX_TOKEN_LINE_TERMINATOR;
    token->location.start = *current;
    token->location.end = *current;
    noix_position_next(&token->location.end);
    return token;
  }
  return NULL;
}

noix_token_t noix_read_template_string_token(noix_allocator_t allocator,
                                             noix_position_t *current) {
  noix_position_t start = *current;
  noix_position_t end = *current;
  if (*current->offset != '`') {
    return NULL;
  }
  noix_token_t token = noix_create_token(allocator);
  token->type = NOIX_TOKEN_TEMPLATE_STRING;
  token->location.start = start;
  token->location.end = end;
  noix_position_next(&token->location.end);
  return token;
}

noix_token_t noix_read_template_string_start_token(noix_allocator_t allocator,
                                                   noix_position_t *current);

noix_token_t noix_read_template_string_end_token(noix_allocator_t allocator,
                                                 noix_position_t *current);

noix_token_t noix_read_template_string_part_token(noix_allocator_t allocator,
                                                  noix_position_t *current);