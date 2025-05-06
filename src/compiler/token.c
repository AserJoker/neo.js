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
      } else {
        current.column++;
        current.offset++;
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

noix_token_t noix_read_number_token(noix_allocator_t allocator,
                                    const char *file,
                                    noix_position_t *position) {
  noix_position_t current = *position;
  if (*current.offset == '0' &&
      (*(current.offset + 1) == 'x' || *(current.offset + 1) == 'X')) {
    current.offset += 2;
    current.column += 2;
    if ((*current.offset >= '0' && *current.offset <= '9') ||
        (*current.offset >= 'a' && *current.offset <= 'f') ||
        (*current.offset >= 'A' && *current.offset <= 'F')) {
      while ((*current.offset >= '0' && *current.offset <= '9') ||
             (*current.offset >= 'a' && *current.offset <= 'f') ||
             (*current.offset >= 'A' && *current.offset <= 'F')) {
        current.offset++;
        current.column++;
      }
    } else {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      return NULL;
    }
  } else if (*current.offset == '0' &&
             (*(current.offset + 1) == 'o' || *(current.offset + 1) == 'O')) {
    current.offset += 2;
    current.column += 2;
    if ((*current.offset >= '0' && *current.offset <= '7')) {
      while ((*current.offset >= '0' && *current.offset <= '7')) {
        current.offset++;
        current.column++;
      }
    } else {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      return NULL;
    }
  } else {
    if (*current.offset == '.' && *(current.offset + 1) >= '0' &&
        *(current.offset + 1) <= '9') {
      current.offset += 2;
      current.column += 2;
      while ((*current.offset >= '0' && *current.offset <= '9')) {
        current.offset++;
        current.column++;
      }
    } else if (*current.offset >= '0' && *current.offset <= '9') {
      current.offset += 1;
      current.column += 1;
      while ((*current.offset >= '0' && *current.offset <= '9')) {
        current.offset++;
        current.column++;
      }
      if (*current.offset == '.') {
        current.offset += 1;
        current.column += 1;
        while ((*current.offset >= '0' && *current.offset <= '9')) {
          current.offset++;
          current.column++;
        }
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
      if (*current.offset >= '0' && *current.offset <= '9') {
        while ((*current.offset >= '0' && *current.offset <= '9')) {
          current.offset++;
          current.column++;
        }
      } else {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        return NULL;
      }
    }
  }

  noix_token_t token = (noix_token_t)noix_allocator_alloc(
      allocator, sizeof(struct _noix_token_t), NULL);
  token->position.begin = *position;
  token->position.end = current;
  token->position.file = file;
  token->type = NOIX_TOKEN_TYPE_NUMBER;
  *position = current;
  return token;
}

noix_token_t noix_read_symbol_token(noix_allocator_t allocator,
                                    const char *file,
                                    noix_position_t *position) {
  static const char *operators[] = {
      ">>>=", "...", "<<=", ">>>", "===", "!==", "**=", ">>=", "&&=", R"(??=)",
      "**",   "==",  "!=",  "<<",  ">>",  "<=",  ">=",  "&&",  "||",  "??",
      "++",   "--",  "+=",  "-=",  "*=",  "/=",  "%=",  "||=", "&=",  "^=",
      "|=",   "=>",  "?.",  "=",   "*",   "/",   "%",   "+",   "-",   "<",
      ">",    "&",   "^",   "|",   ",",   "!",   "~",   "(",   ")",   "[",
      "]",    "{",   "}",   "@",   "#",   ".",   "?",   ":",   ";",   0,
  };
  noix_position_t current = *position;
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
  noix_token_t token = (noix_token_t)noix_allocator_alloc(
      allocator, sizeof(struct _noix_token_t), NULL);
  token->position.begin = *position;
  token->position.end = current;
  token->position.file = file;
  token->type = NOIX_TOKEN_TYPE_SYMBOL;
  *position = current;
  return token;
}

noix_token_t noix_read_regexp_token(noix_allocator_t allocator,
                                    const char *file,
                                    noix_position_t *position) {
  noix_position_t current = *position;
  if (*current.offset == '/' && *(current.offset + 1) != '/') {
    current.offset++;
    current.column++;
    int32_t level = 0;
    while (true) {
      noix_utf8_char chr = noix_utf8_read_char(current.offset);
      if (*chr.begin == 0xa || *chr.begin == 0xd || *chr.begin == '\0' ||
          noix_utf8_char_is(chr, "\u2028") ||
          noix_utf8_char_is(chr, "\u2029")) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        return NULL;
      }
      if (*chr.begin == '/' && level == 0) {
        current.offset++;
        current.column++;
        break;
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
                      "Invalid hexadecimal escape sequence \n  at %s:%d:%d",
                      file, current.line, current.column);
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
                      "Invalid hexadecimal escape sequence \n  at %s:%d:%d",
                      file, current.line, current.column);
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
        } else {
          current.offset++;
          current.column++;
          continue;
        }
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
        THROW("SyntaxError", "Invalid regular expression flags \n  at %s:%d:%d",
              file, current.line, current.column);
        return NULL;
      }
      current.offset++;
      current.column++;
    }
  } else {
    return NULL;
  }
  noix_token_t token = (noix_token_t)noix_allocator_alloc(
      allocator, sizeof(struct _noix_token_t), NULL);
  token->position.begin = *position;
  token->position.end = current;
  token->position.file = file;
  token->type = NOIX_TOKEN_TYPE_REGEXP;
  *position = current;
  return token;
}

noix_token_t noix_read_identify_token(noix_allocator_t allocator,
                                      const char *file,
                                      noix_position_t *position) {

  noix_position_t current = *position;
  noix_utf8_char chr = noix_utf8_read_char(current.offset);
  if (*current.offset == '\\' && *(current.offset + 1) == 'u') {
    noix_position_t backup = current;
    current.offset += 2;
    current.column += 2;
    uint32_t utf32 = 0;
    if (*current.offset == '{') {
      current.offset++;
      current.column++;
      if ((*current.offset >= '0' && *current.offset <= '9') ||
          (*current.offset >= 'a' && *current.offset <= 'f') ||
          (*current.offset >= 'A' && *current.offset <= 'F')) {
        while ((*current.offset >= '0' && *current.offset <= '9') ||
               (*current.offset >= 'a' && *current.offset <= 'f') ||
               (*current.offset >= 'A' && *current.offset <= 'F')) {
          utf32 *= 16;
          if (*current.offset >= '0' && *current.offset <= '9') {
            utf32 += *current.offset - '0';
          } else if (*current.offset >= 'a' && *current.offset <= 'f') {
            utf32 += *current.offset - 'a' + 10;
          } else if (*current.offset >= 'A' && *current.offset <= 'F') {
            utf32 += *current.offset - 'A' + 10;
          }
          current.offset++;
          current.column++;
        }
        if (*current.offset != '}') {
          current = backup;
          THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
                file, current.line, current.column);
          return NULL;
        } else {
          current.offset++;
          current.column++;
        }
        if (utf32 != '$' && utf32 != '_' && !IS_ID_START(utf32)) {
          current = backup;
          THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
                file, current.line, current.column);
          return NULL;
        }
      } else {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        return NULL;
      }
    } else if ((*current.offset >= '0' && *current.offset <= '9') ||
               (*current.offset >= 'a' && *current.offset <= 'f') ||
               (*current.offset >= 'A' && *current.offset <= 'F')) {
      for (int32_t idx = 0; idx < 4; idx++) {
        utf32 *= 16;
        if (*current.offset >= '0' && *current.offset <= '9') {
          utf32 += *current.offset - '0';
        } else if (*current.offset >= 'a' && *current.offset <= 'f') {
          utf32 += *current.offset - 'a' + 10;
        } else if (*current.offset >= 'A' && *current.offset <= 'F') {
          utf32 += *current.offset - 'A' + 10;
        } else {
          THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
                file, current.line, current.column);
          return NULL;
        }
        current.offset++;
        current.column++;
      }
    }
    if (utf32 != '$' && utf32 != '_' && !IS_ID_START(utf32)) {
      current = backup;
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      return NULL;
    }
  }
  if (!noix_utf8_char_is_id_start(chr) && !noix_utf8_char_is(chr, "$") &&
      !noix_utf8_char_is(chr, "_")) {
    return NULL;
  }
  current.column += chr.end - chr.begin;
  current.offset = chr.end;
  chr = noix_utf8_read_char(current.offset);
  while (true) {
    if (*current.offset == '\\' && *(current.offset + 1) == 'u') {
      noix_position_t backup = current;
      current.offset += 2;
      current.column += 2;
      uint32_t utf32 = 0;
      if (*current.offset == '{') {
        current.offset++;
        current.column++;
        if ((*current.offset >= '0' && *current.offset <= '9') ||
            (*current.offset >= 'a' && *current.offset <= 'f') ||
            (*current.offset >= 'A' && *current.offset <= 'F')) {
          while ((*current.offset >= '0' && *current.offset <= '9') ||
                 (*current.offset >= 'a' && *current.offset <= 'f') ||
                 (*current.offset >= 'A' && *current.offset <= 'F')) {
            utf32 *= 16;
            if (*current.offset >= '0' && *current.offset <= '9') {
              utf32 += *current.offset - '0';
            } else if (*current.offset >= 'a' && *current.offset <= 'f') {
              utf32 += *current.offset - 'a' + 10;
            } else if (*current.offset >= 'A' && *current.offset <= 'F') {
              utf32 += *current.offset - 'A' + 10;
            }
            current.offset++;
            current.column++;
          }
          if (*current.offset != '}') {
            current = backup;
            THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
                  file, current.line, current.column);
            return NULL;
          } else {
            current.offset++;
            current.column++;
          }
          if (utf32 != '$' && utf32 != '_' && !IS_ID_CONTINUE(utf32)) {
            current = backup;
            THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
                  file, current.line, current.column);
            return NULL;
          }
        } else {
          THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
                file, current.line, current.column);
          return NULL;
        }
      } else if ((*current.offset >= '0' && *current.offset <= '9') ||
                 (*current.offset >= 'a' && *current.offset <= 'f') ||
                 (*current.offset >= 'A' && *current.offset <= 'F')) {
        for (int32_t idx = 0; idx < 4; idx++) {
          utf32 *= 16;
          if (*current.offset >= '0' && *current.offset <= '9') {
            utf32 += *current.offset - '0';
          } else if (*current.offset >= 'a' && *current.offset <= 'f') {
            utf32 += *current.offset - 'a' + 10;
          } else if (*current.offset >= 'A' && *current.offset <= 'F') {
            utf32 += *current.offset - 'A' + 10;
          } else {
            THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
                  file, current.line, current.column);
            return NULL;
          }
          current.offset++;
          current.column++;
        }
      } else {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        return NULL;
      }
      if (utf32 != '$' && utf32 != '_' && !IS_ID_CONTINUE(utf32)) {
        current = backup;
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        return NULL;
      }
    } else if (noix_utf8_char_is_id_continue(chr) ||
               noix_utf8_char_is(chr, "$") || noix_utf8_char_is(chr, "_")) {
      current.column += chr.end - chr.begin;
      current.offset = chr.end;
    } else {
      break;
    }
    chr = noix_utf8_read_char(current.offset);
  }
  noix_token_t token = (noix_token_t)noix_allocator_alloc(
      allocator, sizeof(struct _noix_token_t), NULL);
  token->position.begin = *position;
  token->position.end = current;
  token->position.file = file;
  token->type = NOIX_TOKEN_TYPE_IDENTIFY;
  *position = current;
  return token;
}