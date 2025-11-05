#include "engine/stackframe.h"
#include "core/allocator.h"
#include "core/string.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

neo_js_stackframe_t neo_create_js_stackframe(neo_allocator_t allocator,
                                             const uint16_t *filename,
                                             const uint16_t *funcname,
                                             uint32_t line, uint32_t column) {
  neo_js_stackframe_t frame =
      neo_allocator_alloc(allocator, sizeof(struct _neo_js_stackframe_t), NULL);
  frame->filename = filename;
  frame->funcname = funcname;
  frame->column = column;
  frame->line = line;
  return frame;
}
uint16_t *neo_js_stackframe_to_string(neo_allocator_t allocator,
                                      neo_js_stackframe_t frame) {
  size_t len = neo_string16_length(frame->funcname);
  if (frame->filename) {
    len += neo_string16_length(frame->filename) + 64;
  } else {
    len += 16;
  }
  uint16_t *string =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * len, NULL);
  uint16_t *dst = string;
  const uint16_t *src = frame->funcname;
  while (*src) {
    *dst++ = *src++;
  }
  *dst++ = ' ';
  *dst++ = '(';
  if (frame->filename) {
    src = frame->filename;
    while (*src) {
      *dst++ = *src++;
    }
    char s[64];
    sprintf(s, "%d:%d", frame->line, frame->column);
    const char *src = s;
    while (*src) {
      *dst++ = *src++;
    }
  } else {
    const char *internal = "<internal>";
    while (*internal) {
      *dst++ = *internal++;
    }
  }
  *dst++ = ')';
  *dst = 0;
  return string;
}