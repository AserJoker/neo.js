#include "engine/basetype/string.h"
#include "core/allocator.h"
#include "core/string.h"
#include "core/unicode.h"
#include "engine/context.h"
#include "engine/runtime.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *neo_js_string_typeof(neo_js_context_t ctx,
                                        neo_js_variable_t variable) {
  return "string";
}

static neo_js_variable_t neo_js_string_to_string(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  neo_js_string_t string = neo_js_variable_to_string(self);
  const char *cstring = neo_js_context_to_cstring(ctx, self);
  return neo_js_context_create_string(ctx, cstring);
}

static neo_js_variable_t neo_js_string_to_boolean(neo_js_context_t ctx,
                                                  neo_js_variable_t self) {
  neo_js_string_t string = neo_js_variable_to_string(self);
  return neo_js_context_create_boolean(ctx, string->string[0] != 0);
}

static neo_js_variable_t neo_js_string_to_number(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  neo_js_string_t string =
      neo_js_value_to_string(neo_js_variable_get_value(self));
  const char *cstring = neo_js_context_to_cstring(ctx, self);
  if (strcmp(cstring, "Infinity") == 0) {
    return neo_js_context_create_number(ctx, INFINITY);
  }
  if (strcmp(cstring, "-Infinity") == 0) {
    return neo_js_context_create_number(ctx, -INFINITY);
  }
  char *end = 0;
  double val = strtod(cstring, &end);
  if (*end != 0) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, val);
}

static neo_js_variable_t neo_js_string_to_object(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  return neo_js_context_construct(
      ctx, neo_js_context_get_std(ctx).string_constructor, 1, &self);
}

static bool neo_js_string_is_equal(neo_js_context_t ctx, neo_js_variable_t self,
                                   neo_js_variable_t another) {
  neo_js_value_t val1 = neo_js_variable_get_value(self);
  neo_js_value_t val2 = neo_js_variable_get_value(another);
  neo_js_string_t str1 = neo_js_value_to_string(val1);
  neo_js_string_t str2 = neo_js_value_to_string(val2);
  const uint16_t *s1 = str1->string;
  const uint16_t *s2 = str2->string;
  for (;;) {
    if (*s1 != *s2) {
      return false;
    }
    if (!*s1) {
      break;
    }
    s1++;
    s2++;
  }
  return true;
}
static neo_js_variable_t neo_js_string_copy(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            neo_js_variable_t target) {
  neo_js_string_t string =
      neo_js_value_to_string(neo_js_variable_get_value(self));
  neo_js_chunk_t htarget = neo_js_variable_get_chunk(target);
  neo_allocator_t allocaotr =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  const char *cstring = neo_js_context_to_cstring(ctx, self);
  neo_js_chunk_set_value(allocaotr, htarget,
                         &neo_create_js_string(allocaotr, cstring)->value);
  return target;
}
neo_js_type_t neo_get_js_string_type() {
  static struct _neo_js_type_t type = {
      NEO_JS_TYPE_STRING,
      neo_js_string_typeof,
      neo_js_string_to_string,
      neo_js_string_to_boolean,
      neo_js_string_to_number,
      NULL,
      neo_js_string_to_object,
      NULL,
      NULL,
      NULL,
      neo_js_string_is_equal,
      neo_js_string_copy,
  };
  return &type;
}

static void neo_js_string_dispose(neo_allocator_t allocator,
                                  neo_js_string_t self) {
  neo_allocator_free(allocator, self->string);
  neo_js_value_dispose(allocator, &self->value);
}

neo_js_string_t neo_create_js_string(neo_allocator_t allocator,
                                     const char *value) {
  if (!value) {
    value = "";
  }
  size_t len = strlen(value);
  neo_js_string_t string = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_string_t), neo_js_string_dispose);
  neo_js_value_init(allocator, &string->value);
  string->value.type = neo_get_js_string_type();
  string->string =
      neo_allocator_alloc(allocator, (len + 1) * sizeof(uint16_t), NULL);
  uint16_t *dst = string->string;
  const char *src = value;
  while (*src) {
    neo_utf8_char utf8 = neo_utf8_read_char(src);
    uint32_t utf32 = neo_utf8_char_to_utf32(utf8);
    dst += neo_utf32_to_utf16(utf32, dst);
    src = utf8.end;
  }
  *dst = 0;
  return string;
}

neo_js_string_t neo_create_js_string16(neo_allocator_t allocator,
                                       const uint16_t *value) {

  size_t len = neo_string16_length(value);
  neo_js_string_t string = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_string_t), neo_js_string_dispose);
  neo_js_value_init(allocator, &string->value);
  string->value.type = neo_get_js_string_type();
  string->string =
      neo_allocator_alloc(allocator, (len + 1) * sizeof(uint16_t), NULL);
  uint16_t *dst = string->string;
  dst[len] = 0;
  memcpy(dst, value, len * sizeof(uint16_t));
  return string;
}

void neo_js_string_set_cstring(neo_allocator_t allocator,
                               neo_js_variable_t variable, const char *value) {
  size_t len = strlen(value);
  neo_js_string_t string = neo_js_variable_to_string(variable);
  neo_allocator_free(allocator, string->string);
  string->string =
      neo_allocator_alloc(allocator, (len + 1) * sizeof(uint16_t), NULL);
  uint16_t *dst = string->string;
  const char *src = value;
  while (*src) {
    neo_utf8_char utf8 = neo_utf8_read_char(src);
    uint32_t utf32 = neo_utf8_char_to_utf32(utf8);
    dst += neo_utf32_to_utf16(utf32, dst);
    src = utf8.end;
  }
  *dst = 0;
}

char *neo_js_string_to_cstring(neo_allocator_t allocator,
                               neo_js_variable_t variable) {
  neo_js_string_t str = neo_js_variable_to_string(variable);
  size_t max = NEO_STRING_CHUNK_SIZE;
  char *s = neo_allocator_alloc(allocator, max, NULL);
  s[0] = 0;
  const uint16_t *src = str->string;
  while (*src) {
    neo_utf16_char chr = neo_utf16_read_char(src);
    src = chr.end;
    uint32_t utf32 = neo_utf16_to_utf32(chr);
    char buf[5];
    size_t size = neo_utf32_to_utf8(utf32, buf);
    buf[size] = 0;
    s = neo_string_concat(allocator, s, &max, buf);
  }
  return s;
}

neo_js_string_t neo_js_value_to_string(neo_js_value_t value) {
  if (value->type->kind == NEO_JS_TYPE_STRING) {
    return (neo_js_string_t)value;
  }
  return NULL;
}
