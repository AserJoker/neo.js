#include "engine/stackframe.h"
#include "core/allocator.h"
#include "core/string.h"
#include "core/unicode.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void neo_js_stackframe_dispose(neo_allocator_t allocator,
                                      neo_js_stackframe_t self) {
  neo_allocator_free(allocator, self->funcname);
}

neo_js_stackframe_t neo_create_js_stackframe(neo_allocator_t allocator,
                                             const char *filename,
                                             const uint16_t *funcname,
                                             uint32_t line, uint32_t column) {
  neo_js_stackframe_t frame =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_stackframe_t),
                          neo_js_stackframe_dispose);
  if (filename) {
    frame->filename = filename;
  } else {
    frame->filename = NULL;
  }
  if (funcname && *funcname) {
    frame->funcname = neo_create_string16(allocator, funcname);
  } else {
    frame->funcname = NULL;
  }
  frame->column = column;
  frame->line = line;
  return frame;
}
uint16_t *neo_js_stackframe_to_string(neo_allocator_t allocator,
                                      neo_js_stackframe_t frame) {
  size_t len = frame->funcname ? neo_string16_length(frame->funcname) : 0;
  if (frame->filename) {
    len += strlen(frame->filename) + 64;
  } else {
    len += 16;
  }
  uint16_t *string =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * len, NULL);
  uint16_t *dst = string;
  const uint16_t *src = frame->funcname;
  if (src) {
    while (*src) {
      *dst++ = *src++;
    }
  }
  if (frame->funcname) {
    *dst++ = ' ';
    *dst++ = '(';
  }
  if (frame->filename) {
    const char *src = frame->filename;
    while (*src) {
      neo_utf8_char chr = neo_utf8_read_char(src);
      src = chr.end;
      uint32_t utf32 = neo_utf8_char_to_utf32(chr);
      dst += neo_utf32_to_utf16(utf32, dst);
    }
    char s[64];
    sprintf(s, ":%d:%d", frame->line, frame->column);
    src = s;
    while (*src) {
      *dst++ = *src++;
    }
  } else {
    const char *internal = "<internal>";
    while (*internal) {
      *dst++ = *internal++;
    }
  }
  if (frame->funcname) {
    *dst++ = ')';
  }
  *dst = 0;
  return string;
}