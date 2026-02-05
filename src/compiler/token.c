#include "neojs/compiler/token.h"
#include "neojs/core/allocator.h"
#include "neojs/core/position.h"
#include "neojs/core/unicode.h"
#include <stdarg.h>
#include <stdio.h>
#include <uchar.h>
#include <unicode/urename.h>

#define IS_HEX(ch)                                                             \
  (((ch) >= '0' && (ch) <= '9') || ((ch) >= 'a' && (ch) <= 'f') ||             \
   ((ch) >= 'A' && (ch) <= 'F'))

#define IS_OCT(ch) ((ch) >= '0' && (ch) <= '7')

#define IS_DEC(ch) ((ch) >= '0' && (ch) <= '9')

static int32_t neo_read_hex(const char *file, neo_position_t *position) {
  neo_position_t current = *position;
  if (IS_HEX(*current.offset) ||
      (*current.offset == '_' && *(current.offset - 1) != '_')) {
    while (IS_HEX(*current.offset) ||
           (*current.offset == '_' && *(current.offset - 1) != '_')) {
      current.offset++;
      current.column++;
    }
  } else {
    return -1;
  }
  int32_t size = current.offset - position->offset;
  *position = current;
  return size;
}

static int32_t neo_read_oct(const char *file, neo_position_t *position) {
  neo_position_t current = *position;
  if (IS_OCT(*current.offset) ||
      (*current.offset == '_' && *(current.offset - 1) != '_')) {
    while (IS_OCT(*current.offset) ||
           (*current.offset == '_' && *(current.offset - 1) != '_')) {
      current.offset++;
      current.column++;
    }
  } else {
    return -1;
  }
  int32_t size = current.offset - position->offset;
  *position = current;
  return size;
}

static int32_t neo_read_dec(const char *file, neo_position_t *position) {
  neo_position_t current = *position;
  if (IS_DEC(*current.offset) ||
      (*current.offset == '_' && *(current.offset - 1) != '_')) {
    while (IS_DEC(*current.offset) ||
           (*current.offset == '_' && *(current.offset - 1) != '_')) {
      current.offset++;
      current.column++;
    }
  } else {
    return -1;
  }
  int32_t size = current.offset - position->offset;
  *position = current;
  return size;
}

static int32_t neo_read_escape(neo_allocator_t allocator, const char *file,
                               neo_position_t *position, neo_token_t *error) {
  neo_position_t current = *position;
  if (*current.offset == '\\') {
    current.offset++;
    current.column++;
    if (*current.offset == 'u') {
      current.offset++;
      current.column++;
      if (*current.offset == '{') {
        current.offset++;
        current.column++;
        neo_read_hex(file, &current);
        if (*current.offset != '}') {
          *error = neo_create_error_token(
              allocator,
              "Invalid hexadecimal escape sequence \n  at _.compile "
              "(%s:%d:%d)",
              file, current.line, current.column);
          return -1;
        } else {
          current.offset++;
          current.column++;
        }
      } else {
        for (int32_t idx = 0; idx < 4; idx++) {
          if (!IS_HEX(*current.offset)) {
            *error = neo_create_error_token(
                allocator,
                "Invalid hexadecimal escape sequence \n  at "
                "_.compile (%s:%d:%d)",
                file, current.line, current.column);
            return -1;
          }
          current.offset++;
          current.column++;
        }
      }
    } else if (*current.offset == 'x') {
      current.offset++;
      current.column++;
      for (int32_t idx = 0; idx < 2; idx++) {
        if (!IS_HEX(*current.offset)) {
          *error = neo_create_error_token(
              allocator,
              "Invalid hexadecimal escape sequence \n  at _.compile "
              "(%s:%d:%d)",
              file, current.line, current.column);
          return -1;
        }
        current.offset++;
        current.column++;
      }
    } else {
      current.offset++;
      current.column++;
    }
  } else {
    return 0;
  }
  int32_t size = current.offset - position->offset;
  *position = current;
  return size;
}

static void neo_token_dispose(neo_allocator_t allocaotr, neo_token_t self) {
  if (self->type == NEO_TOKEN_TYPE_ERROR) {
    neo_allocator_free(allocaotr, self->error);
  }
}

neo_token_t neo_create_error_token(neo_allocator_t allocator,
                                   const char *message, ...) {
  neo_token_t token = neo_allocator_alloc(
      allocator, sizeof(struct _neo_token_t), neo_token_dispose);
  token->type = NEO_TOKEN_TYPE_ERROR;
  va_list args;
  token->error = neo_allocator_alloc(allocator, 4096, NULL);
  va_start(args, message);
  vsnprintf(token->error, 4096, message, args);
  va_end(args);
  return token;
}

neo_token_t neo_read_string_token(neo_allocator_t allocator, const char *file,
                                  neo_position_t *position) {
  neo_position_t current = *position;
  if (*current.offset != '\'' && *current.offset != '\"') {
    return NULL;
  }
  current.column++;
  current.offset++;
  while (true) {
    neo_utf8_char chr = neo_utf8_read_char(current.offset);
    if (*chr.begin == 0xa || *chr.begin == 0xd || *chr.begin == '\0' ||
        neo_utf8_char_is(chr, "\u2028") || neo_utf8_char_is(chr, "\u2029")) {
      return neo_create_error_token(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
    }
    if (neo_utf8_char_is(chr, "\\")) {
      neo_token_t error = NULL;
      if (neo_read_escape(allocator, file, &current, &error) == -1) {
        return error;
      };
      continue;
    }
    if (*chr.begin == *position->offset) {
      current.column++;
      current.offset++;
      break;
    }
    current.offset = chr.end;
    current.column += chr.end - chr.begin;
  }
  neo_token_t token = (neo_token_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_token_t), NULL);
  token->type = NEO_TOKEN_TYPE_STRING;
  token->location.begin = *position;
  token->location.end = current;
  token->location.file = file;
  token->type = NEO_TOKEN_TYPE_STRING;
  *position = current;
  return token;
}

neo_token_t neo_read_number_token(neo_allocator_t allocator, const char *file,
                                  neo_position_t *position) {
  neo_position_t current = *position;
  if (*current.offset == '0' &&
      (*(current.offset + 1) == 'x' || *(current.offset + 1) == 'X')) {
    current.offset += 2;
    current.column += 2;
    if (IS_HEX(*current.offset)) {
      neo_read_hex(file, &current);
      neo_utf8_char chr = neo_utf8_read_char(current.offset);
      if (neo_utf8_char_is_id_start(chr)) {
        return neo_create_error_token(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
      }
    } else {
      return neo_create_error_token(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
    }
  } else if (*current.offset == '0' &&
             (*(current.offset + 1) == 'o' || *(current.offset + 1) == 'O')) {
    current.offset += 2;
    current.column += 2;
    if (IS_OCT(*current.offset)) {
      neo_read_oct(file, &current);
      neo_utf8_char chr = neo_utf8_read_char(current.offset);
      char32_t utf32 = neo_utf8_char_to_utf32(chr);
      if (neo_utf8_char_is_id_start(chr) || chr.end - chr.begin > 1 ||
          (*chr.begin >= '8' && *chr.end <= '9')) {
        return neo_create_error_token(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
      }
    } else {
      return neo_create_error_token(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
      return NULL;
    }
  } else {
    if (*current.offset == '.' && IS_DEC(*(current.offset + 1))) {
      current.offset += 2;
      current.column += 2;
      neo_read_dec(file, &current);
    } else if (IS_DEC(*current.offset)) {
      current.offset += 1;
      current.column += 1;
      neo_read_dec(file, &current);
      if (*current.offset == '.') {
        current.offset += 1;
        current.column += 1;
        neo_read_dec(file, &current);
      }
    } else {
      return NULL;
    }
    if (*current.offset == 'e' || *current.offset == 'E') {
      current.offset++;
      current.column++;
      if (*current.offset == '-' || *current.offset == '+') {
        current.offset++;
        current.column++;
      }
      if (IS_DEC(*current.offset)) {
        neo_read_dec(file, &current);
      } else {
        return neo_create_error_token(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
      }
    }
    neo_utf8_char chr = neo_utf8_read_char(current.offset);
    if (*chr.begin != 'n' && neo_utf8_char_is_id_start(chr)) {
      return neo_create_error_token(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
    }
  }
  neo_token_t token = (neo_token_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_token_t), NULL);
  token->location.begin = *position;
  token->location.end = current;
  token->location.file = file;
  token->type = NEO_TOKEN_TYPE_NUMBER;
  *position = current;
  return token;
}

neo_token_t neo_read_symbol_token(neo_allocator_t allocator, const char *file,
                                  neo_position_t *position) {
  static const char *operators[] = {
      ">>>=",   "...", "<<=", ">>>", "===", "!==", "**=", ">>=", "&&=", "||=",
      "(?\?=)", "**",  "==",  "!=",  "<<",  ">>",  "<=",  ">=",  "&&",  "||",
      "??",     "++",  "--",  "+=",  "-=",  "*=",  "/=",  "%=",  "&=",  "^=",
      "|=",     "=>",  "?.",  "=",   "*",   "/",   "%",   "+",   "-",   "<",
      ">",      "&",   "^",   "|",   ",",   "!",   "~",   "(",   ")",   "[",
      "]",      "{",   "}",   "@",   "#",   ".",   "?",   ":",   ";",   0,
  };
  neo_position_t current = *position;
  int32_t idx = 0;
  const char *pstr = NULL;
  for (; operators[idx] != 0; idx++) {
    const char *opt = operators[idx];
    pstr = current.offset;
    while (*opt) {
      if (*opt != *pstr) {
        break;
      }
      opt++;
      pstr++;
    }
    if (!*opt) {
      break;
    }
  }
  if (operators[idx] == 0) {
    return NULL;
  }
  current.column += pstr - current.offset;
  current.offset = pstr;
  neo_token_t token = (neo_token_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_token_t), NULL);
  token->location.begin = *position;
  token->location.end = current;
  token->location.file = file;
  token->type = NEO_TOKEN_TYPE_SYMBOL;
  *position = current;
  return token;
}

neo_token_t neo_read_regexp_token(neo_allocator_t allocator, const char *file,
                                  neo_position_t *position) {
  neo_position_t current = *position;
  if (*current.offset == '/' && *(current.offset + 1) != '/') {
    current.offset++;
    current.column++;
    int32_t level = 0;
    while (true) {
      neo_utf8_char chr = neo_utf8_read_char(current.offset);
      if (*chr.begin == 0xa || *chr.begin == 0xd || *chr.begin == '\0' ||
          neo_utf8_char_is(chr, "\u2028") || neo_utf8_char_is(chr, "\u2029")) {
        return neo_create_error_token(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
      }
      if (*chr.begin == '/' && level == 0) {
        current.offset++;
        current.column++;
        break;
      }
      if (neo_utf8_char_is(chr, "\\")) {
        neo_token_t error = NULL;
        if (neo_read_escape(allocator, file, &current, &error) == -1) {
          return error;
        };
        continue;
      }
      if (*chr.begin == '[') {
        level++;
      }
      if (*chr.begin == ']') {
        level--;
      }
      current.offset = chr.end;
      current.column += chr.end - chr.begin;
    }
    while (*current.offset >= 'a' && *current.offset <= 'z') {
      if (*current.offset != 'd' && *current.offset != 'g' &&
          *current.offset != 'i' && *current.offset != 'm' &&
          *current.offset != 'u' && *current.offset != 'y' &&
          *current.offset != 's') {
        return neo_create_error_token(
            allocator,
            "Invalid regular expression flags \n  at _.compile (%s:%d:%d)",
            file, current.line, current.column);
      }
      current.offset++;
      current.column++;
    }
  } else {
    return NULL;
  }
  neo_token_t token = (neo_token_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_token_t), NULL);
  token->location.begin = *position;
  token->location.end = current;
  token->location.file = file;
  token->type = NEO_TOKEN_TYPE_REGEXP;
  *position = current;
  return token;
}

neo_token_t neo_read_identify_token(neo_allocator_t allocator, const char *file,
                                    neo_position_t *position) {

  neo_position_t current = *position;
  neo_utf8_char chr = neo_utf8_read_char(current.offset);
  if (*current.offset == '\\' && *(current.offset + 1) == 'u') {
    const char *start = current.offset + 2;
    neo_token_t error = NULL;
    if (neo_read_escape(allocator, file, &current, &error) == -1) {
      return error;
    };
    uint32_t utf32 = 0;
    if (*start == '{') {
      start++;
      while (*start != '}') {
        utf32 *= 16;
        if (*start >= '0' && *start <= '9') {
          utf32 += *start - '0';
        }
        if (*start >= 'a' && *start <= 'f') {
          utf32 += *start - 'a' + 10;
        }
        if (*start >= 'A' && *start <= 'F') {
          utf32 += *start - 'A' + 10;
        }
        start++;
      }
      start++;
    } else {
      for (int8_t idx = 0; idx < 4; idx++) {
        utf32 *= 16;
        if (*start >= '0' && *start <= '9') {
          utf32 += *start - '0';
        }
        if (*start >= 'a' && *start <= 'f') {
          utf32 += *start - 'a' + 10;
        }
        if (*start >= 'A' && *start <= 'F') {
          utf32 += *start - 'A' + 10;
        }
        start++;
      }
    }
  } else if (!neo_utf8_char_is_id_start(chr) && !neo_utf8_char_is(chr, "$") &&
             !neo_utf8_char_is(chr, "_") && !neo_utf8_char_is(chr, "#")) {
    return NULL;
  } else {
    current.column += chr.end - chr.begin;
    current.offset = chr.end;
  }

  chr = neo_utf8_read_char(current.offset);
  while (true) {
    if (*current.offset == '\\' && *(current.offset + 1) == 'u') {
      const char *start = current.offset + 2;
      neo_token_t error = NULL;
      if (neo_read_escape(allocator, file, &current, &error) == -1) {
        return error;
      };
      int32_t utf32 = 0;
      if (*start == '{') {
        start++;
        while (*start != '}') {
          utf32 *= 16;
          if (*start >= '0' && *start <= '9') {
            utf32 += *start - '0';
          }
          if (*start >= 'a' && *start <= 'f') {
            utf32 += *start - 'a' + 10;
          }
          if (*start >= 'A' && *start <= 'F') {
            utf32 += *start - 'F' + 10;
          }
          start++;
        }
      } else {
        for (int8_t idx = 0; idx < 4; idx++) {
          utf32 *= 16;
          if (*start >= '0' && *start <= '9') {
            utf32 += *start - '0';
          }
          if (*start >= 'a' && *start <= 'f') {
            utf32 += *start - 'a' + 10;
          }
          if (*start >= 'A' && *start <= 'F') {
            utf32 += *start - 'F' + 10;
          }
          start++;
        }
      }
      if (utf32 != '$' && utf32 != '_' && !u_isJavaIDPart(utf32)) {
        break;
      } else {
        current.column += chr.end - chr.begin;
        current.offset = chr.end;
      }
    } else if (neo_utf8_char_is_id_continue(chr) ||
               neo_utf8_char_is(chr, "$") || neo_utf8_char_is(chr, "_")) {
      current.column += chr.end - chr.begin;
      current.offset = chr.end;
    } else {
      break;
    }
    chr = neo_utf8_read_char(current.offset);
  }
  if (*current.offset == '#') {
    return neo_create_error_token(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
  }
  neo_token_t token = (neo_token_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_token_t), NULL);
  token->location.begin = *position;
  token->location.end = current;
  token->location.file = file;
  token->type = NEO_TOKEN_TYPE_IDENTIFY;
  *position = current;
  return token;
}

neo_token_t neo_read_comment_token(neo_allocator_t allocator, const char *file,
                                   neo_position_t *position) {
  neo_position_t current = *position;
  if (*current.offset == '/' && *(current.offset + 1) == '/') {
    current.offset += 2;
    current.column += 2;
    while (true) {
      neo_utf8_char chr = neo_utf8_read_char(current.offset);
      if (*chr.begin == 0xa || *chr.begin == 0xd || *chr.begin == '\0' ||
          neo_utf8_char_is(chr, "\u2028") || neo_utf8_char_is(chr, "\u2029")) {
        break;
      } else {
        current.column += chr.end - chr.begin;
        current.offset = chr.end;
      }
    }
  } else {
    return NULL;
  }
  neo_token_t token = (neo_token_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_token_t), NULL);
  token->location.begin = *position;
  token->location.end = current;
  token->location.file = file;
  token->type = NEO_TOKEN_TYPE_COMMENT;
  *position = current;
  return token;
}

neo_token_t neo_read_multiline_comment_token(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  if (*current.offset == '/' && *(current.offset + 1) == '*') {
    current.offset += 2;
    current.column += 2;
    while (true) {
      if (*current.offset == 0) {
        return neo_create_error_token(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
      }
      if (*current.offset == '*' && *(current.offset + 1) == '/') {
        current.offset += 2;
        current.column += 2;
        break;
      }
      neo_utf8_char chr = neo_utf8_read_char(current.offset);
      if (*chr.begin == 0xa || *chr.begin == 0xd ||
          neo_utf8_char_is(chr, "\u2028") || neo_utf8_char_is(chr, "\u2029")) {
        current.line++;
        current.column = 1;
        current.offset = chr.end;
      } else {
        current.column += chr.end - chr.begin;
        current.offset = chr.end;
      }
    }
  } else {
    return NULL;
  }
  neo_token_t token = (neo_token_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_token_t), NULL);
  token->location.begin = *position;
  token->location.end = current;
  token->location.file = file;
  token->type = NEO_TOKEN_TYPE_MULTILINE_COMMENT;
  *position = current;
  return token;
}

neo_token_t neo_read_template_string_token(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position) {
  neo_position_t current = *position;
  if (*current.offset == '`' || *current.offset == '}') {
    current.offset++;
    current.column++;
    while (true) {
      if (*current.offset == '\0') {
        return neo_create_error_token(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
      }
      if (*current.offset == '$' && *(current.offset + 1) == '{') {
        current.column += 2;
        current.offset += 2;
        break;
      }
      if (*current.offset == '`') {
        current.column++;
        current.offset++;
        break;
      }
      neo_utf8_char chr = neo_utf8_read_char(current.offset);
      if (*chr.begin == 0xa || *chr.begin == 0xd ||
          neo_utf8_char_is(chr, "\u2028") || neo_utf8_char_is(chr, "\u2029")) {
        current.line++;
        current.column = 1;
        current.offset = chr.end;
        continue;
      } else if (neo_utf8_char_is(chr, "\\")) {
        neo_token_t error = NULL;
        if (neo_read_escape(allocator, file, &current, &error) == -1) {
          return error;
        };
        continue;
      }
      current.offset = chr.end;
      current.column += chr.end - chr.begin;
    }
  } else {
    return NULL;
  }
  neo_token_t token = (neo_token_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_token_t), NULL);
  token->location.begin = *position;
  token->location.end = current;
  token->location.file = file;
  token->type = NEO_TOKEN_TYPE_TEMPLATE_STRING;
  if (*position->offset == '}') {
    if (*(current.offset - 1) == '`') {
      token->type = NEO_TOKEN_TYPE_TEMPLATE_STRING_END;
    } else {
      token->type = NEO_TOKEN_TYPE_TEMPLATE_STRING_PART;
    }
  } else {
    if (*(current.offset - 1) == '`') {
      token->type = NEO_TOKEN_TYPE_TEMPLATE_STRING;
    } else {
      token->type = NEO_TOKEN_TYPE_TEMPLATE_STRING_START;
    }
  }
  *position = current;
  return token;
}