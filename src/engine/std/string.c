#include "engine/std/string.h"
#include "core/allocator.h"
#include "core/string.h"
#include "core/unicode.h"
#include "engine/basetype/number.h"
#include "engine/context.h"
#include "engine/std/array.h"
#include "engine/std/regexp.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <xkeycheck.h>

NEO_JS_CFUNCTION(neo_js_string_from_char_code) {
  return neo_js_context_create_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_string_from_code_point) {
  return neo_js_context_create_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_string_raw) {
  if (!argc) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Cannot convert undefined or null to object");
  }
  neo_js_variable_t qusis = argv[0];
  if (neo_js_variable_get_type(qusis)->kind < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Cannot convert undefined or null to object");
  }
  neo_js_variable_t vlength = neo_js_context_get_field(
      ctx, qusis, neo_js_context_create_string(ctx, "length"), NULL);
  NEO_JS_TRY_AND_THROW(vlength);
  vlength = neo_js_context_to_integer(ctx, vlength);
  NEO_JS_TRY_AND_THROW(vlength);
  int64_t length = neo_js_variable_to_number(vlength)->number;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  char *result = neo_allocator_alloc(allocator, sizeof(char) * 128, NULL);
  result[0] = 0;
  size_t max = 128;
  for (int64_t idx = 0; idx < length; idx++) {
    neo_js_variable_t vidx = neo_js_context_create_number(ctx, idx);
    neo_js_variable_t str = neo_js_context_get_field(ctx, qusis, vidx, NULL);
    NEO_JS_TRY_AND_THROW(str);
    str = neo_js_context_to_string(ctx, str);
    NEO_JS_TRY_AND_THROW(str);
    result = neo_string_concat(allocator, result, &max, result);
    if (idx < argc) {
      neo_js_variable_t item = argv[0];
      item = neo_js_context_to_string(ctx, item);
      NEO_JS_TRY_AND_THROW(item);
      result = neo_string_concat(allocator, result, &max,
                                 neo_js_context_to_cstring(ctx, item));
    }
  }
  neo_js_context_defer_free(ctx, result);
  return neo_js_context_create_string(ctx, result);
}
NEO_JS_CFUNCTION(neo_js_string_constructor) {
  neo_js_variable_t src = NULL;
  if (argc) {
    src = argv[0];
  } else {
    src = neo_js_context_create_undefined(ctx);
  }
  if (neo_js_context_get_call_type(ctx) == NEO_JS_FUNCTION_CALL) {
    if (neo_js_variable_get_type(src)->kind == NEO_JS_TYPE_SYMBOL) {
      return neo_js_context_create_string(
          ctx, neo_js_variable_to_symbol(src)->string);
    } else {
      return neo_js_context_to_string(ctx, src);
    }
  }
  src = neo_js_context_to_string(ctx, src);
  NEO_JS_TRY_AND_THROW(src);
  neo_js_context_set_internal(ctx, self, "[[primitive]]", src);
  neo_js_context_def_field(
      ctx, self, neo_js_context_create_string(ctx, "length"),
      neo_js_context_create_number(
          ctx, neo_string16_length(neo_js_variable_to_string(src)->string)),
      false, false, false);
  return self;
}
NEO_JS_CFUNCTION(neo_js_string_at) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.at called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  int64_t idx = 0;
  if (argc) {
    neo_js_variable_t vidx = neo_js_context_to_integer(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(vidx);
    idx = neo_js_variable_to_number(vidx)->number;
  }
  neo_js_string_t s = neo_js_variable_to_string(self);
  size_t len = neo_string16_length(s->string);
  if (idx < 0) {
    idx += len;
  }
  if (idx >= len) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, s->string[idx]);
}
NEO_JS_CFUNCTION(neo_js_string_char_at) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.charAt called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  int64_t idx = 0;
  if (argc) {
    neo_js_variable_t vidx = neo_js_context_to_integer(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(vidx);
    idx = neo_js_variable_to_number(vidx)->number;
  }
  neo_js_string_t s = neo_js_variable_to_string(self);
  size_t len = neo_string16_length(s->string);
  neo_js_variable_t res = neo_js_context_create_string(ctx, "");
  if (idx < 0 || idx >= len) {
    return res;
  }
  uint16_t val = s->string[idx];
  neo_js_string_t str = neo_js_variable_to_string(res);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_allocator_free(allocator, str->string);
  str->string = neo_allocator_alloc(allocator, sizeof(uint16_t) * 2, NULL);
  str->string[0] = val;
  str->string[1] = 0;
  return res;
}
NEO_JS_CFUNCTION(neo_js_string_char_code_at) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.charCodeAt called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  int64_t idx = 0;
  if (argc) {
    neo_js_variable_t vidx = neo_js_context_to_integer(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(vidx);
    idx = neo_js_variable_to_number(vidx)->number;
  }
  neo_js_string_t s = neo_js_variable_to_string(self);
  size_t len = neo_string16_length(s->string);
  if (idx < 0 || idx >= len) {
    return neo_js_context_create_number(ctx, NAN);
  }
  uint16_t val = s->string[idx];
  return neo_js_context_create_number(ctx, val);
}
NEO_JS_CFUNCTION(neo_js_string_code_point_at) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.codePointAt called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  int64_t idx = 0;
  if (argc) {
    neo_js_variable_t vidx = neo_js_context_to_integer(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(vidx);
    idx = neo_js_variable_to_number(vidx)->number;
  }
  neo_js_string_t s = neo_js_variable_to_string(self);
  size_t len = neo_string16_length(s->string);
  if (idx < 0 || idx >= len) {
    return neo_js_context_create_undefined(ctx);
  }
  uint16_t *src = &s->string[idx];
  neo_utf16_char chr = neo_utf16_read_char(src);
  return neo_js_context_create_number(ctx, neo_utf16_to_utf32(chr));
}
NEO_JS_CFUNCTION(neo_js_string_concat) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.concat called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  neo_js_variable_t base = self;
  for (uint32_t idx = 0; idx < argc; idx++) {
    neo_js_variable_t item = neo_js_context_to_string(ctx, argv[idx]);
    NEO_JS_TRY_AND_THROW(item);
    base = neo_js_context_concat(ctx, base, item);
  }
  return base;
}
NEO_JS_CFUNCTION(neo_js_string_ends_with) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.endsWith called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  neo_js_variable_t vsearch = NULL;
  if (argc) {
    if (neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t regexp_constructor =
          neo_js_context_get_std(ctx).regexp_constructor;
      neo_js_variable_t res =
          neo_js_context_instance_of(ctx, argv[0], regexp_constructor);
      NEO_JS_TRY_AND_THROW(res);
      if (neo_js_variable_to_boolean(res)->boolean) {
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_TYPE, 0,
            "First argument to String.prototype.endsWith must not be a regular "
            "expression");
      }
    }
    vsearch = neo_js_context_to_string(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(vsearch);
  } else {
    vsearch = neo_js_context_create_string(ctx, "undefined");
  }
  const uint16_t *search = neo_js_variable_to_string(vsearch)->string;
  const uint16_t *source = neo_js_variable_to_string(self)->string;
  int64_t position = 0;
  if (argc > 1) {
    neo_js_variable_t vposition = neo_js_context_to_integer(ctx, argv[1]);
    NEO_JS_TRY_AND_THROW(vposition);
    position = neo_js_variable_to_number(vposition)->number;
  } else {
    position = neo_string16_length(source);
  }
  size_t len = neo_string16_length(source);
  if (position >= len || position < 0) {
    return neo_js_context_create_boolean(ctx, false);
  }
  size_t offset = neo_string16_length(search);
  while (offset > 0) {
    if (source[offset - 1] != search[position - 1]) {
      return neo_js_context_create_boolean(ctx, false);
    }
    offset--;
    position--;
  }
  return neo_js_context_create_boolean(ctx, true);
}
NEO_JS_CFUNCTION(neo_js_string_includes) {

  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.includes called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  neo_js_variable_t vsearch = NULL;
  if (argc) {
    if (neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t regexp_constructor =
          neo_js_context_get_std(ctx).regexp_constructor;
      neo_js_variable_t res =
          neo_js_context_instance_of(ctx, argv[0], regexp_constructor);
      NEO_JS_TRY_AND_THROW(res);
      if (neo_js_variable_to_boolean(res)->boolean) {
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_TYPE, 0,
            "First argument to String.prototype.includes must not be a regular "
            "expression");
      }
    }
    vsearch = neo_js_context_to_string(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(vsearch);
  } else {
    vsearch = neo_js_context_create_string(ctx, "undefined");
  }
  const uint16_t *src = neo_js_variable_to_string(self)->string;
  const uint16_t *search = neo_js_variable_to_string(vsearch)->string;
  return neo_js_context_create_boolean(ctx,
                                       neo_string16_find(src, search) != -1);
}
NEO_JS_CFUNCTION(neo_js_string_index_of) {

  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.indexOf called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  neo_js_variable_t vsearch = NULL;
  if (argc) {
    if (neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t regexp_constructor =
          neo_js_context_get_std(ctx).regexp_constructor;
      neo_js_variable_t res =
          neo_js_context_instance_of(ctx, argv[0], regexp_constructor);
      NEO_JS_TRY_AND_THROW(res);
      if (neo_js_variable_to_boolean(res)->boolean) {
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_TYPE, 0,
            "First argument to String.prototype.indexOf must not be a regular "
            "expression");
      }
    }
    vsearch = neo_js_context_to_string(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(vsearch);
  } else {
    vsearch = neo_js_context_create_string(ctx, "undefined");
  }
  const uint16_t *source = neo_js_variable_to_string(self)->string;
  const uint16_t *search = neo_js_variable_to_string(vsearch)->string;
  int64_t position = 0;
  size_t len = neo_string16_length(source);
  if (argc > 1) {
    neo_js_variable_t vposition = neo_js_context_to_integer(ctx, argv[1]);
    NEO_JS_TRY_AND_THROW(vposition);
    position = neo_js_variable_to_number(vposition)->number;
    if (position < 0) {
      position = 0;
    }
  } else {
    position = 0;
  }
  if (search[0] == 0) {
    if (position >= len) {
      return neo_js_context_create_number(ctx, len);
    } else {
      return neo_js_context_create_number(ctx, position);
    }
  }
  if (position >= len) {
    return neo_js_context_create_number(ctx, -1);
  }
  source = &source[position];
  int64_t res = neo_string16_find(source, search);
  if (res != -1) {
    return neo_js_context_create_number(ctx, res + position);
  } else {
    return neo_js_context_create_number(ctx, -1);
  }
}
NEO_JS_CFUNCTION(neo_js_string_is_well_formed) {

  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.isWellFormed called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  const uint16_t *source = neo_js_variable_to_string(self)->string;
  while (*source) {
    if (*source <= 0xd800) {
      source++;
    } else if (*source > 0xd800 && *source <= 0xdbff) {
      if (*(source + 1) > 0xdc00 && *(source + 1) <= 0xdfff) {
        source += 2;
      } else {
        return neo_js_context_create_boolean(ctx, false);
      }
    } else if (*source > 0xdc00 && *source <= 0xdfff) {
      return neo_js_context_create_boolean(ctx, false);
    }
  }
  return neo_js_context_create_boolean(ctx, true);
}
NEO_JS_CFUNCTION(neo_js_string_last_index_of) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.lastIndexOf called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  neo_js_variable_t vsearch = NULL;
  if (argc) {
    if (neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t regexp_constructor =
          neo_js_context_get_std(ctx).regexp_constructor;
      neo_js_variable_t res =
          neo_js_context_instance_of(ctx, argv[0], regexp_constructor);
      NEO_JS_TRY_AND_THROW(res);
      if (neo_js_variable_to_boolean(res)->boolean) {
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_TYPE, 0,
            "First argument to String.prototype.lastIndexOf must not be a "
            "regular "
            "expression");
      }
    }
    vsearch = neo_js_context_to_string(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(vsearch);
  } else {
    vsearch = neo_js_context_create_string(ctx, "undefined");
  }
  const uint16_t *source = neo_js_variable_to_string(self)->string;
  const uint16_t *search = neo_js_variable_to_string(vsearch)->string;
  int64_t position = 0;
  size_t len = neo_string16_length(source);
  if (argc > 1) {
    neo_js_variable_t vposition = neo_js_context_to_integer(ctx, argv[1]);
    NEO_JS_TRY_AND_THROW(vposition);
    position = neo_js_variable_to_number(vposition)->number;
    if (position < 0) {
      position = 0;
    }
  } else {
    position = 0;
  }
  if (position >= len) {
    position = len;
  }
  if (search[0] == 0) {
    if (position >= len) {
      return neo_js_context_create_number(ctx, len);
    } else {
      return neo_js_context_create_number(ctx, position);
    }
  }
  size_t search_len = neo_string16_length(search);
  uint16_t src[position + 1];
  uint16_t dst[search_len + 1];
  for (size_t idx = 0; idx < position; idx++) {
    src[idx] = source[position - 1 - idx];
  }
  src[position] = 0;
  for (size_t idx = 0; idx < search_len; idx++) {
    dst[idx] = search[search_len - 1 - idx];
  }
  dst[search_len] = 0;
  int64_t res = neo_string16_find(src, dst);
  if (res == -1) {
    return neo_js_context_create_number(ctx, -1);
  } else {
    return neo_js_context_create_number(ctx, position - 1 - res);
  }
}
NEO_JS_CFUNCTION(neo_js_string_local_compare) {
  neo_js_variable_t global = neo_js_context_get_global(ctx);
  neo_js_variable_t intl = neo_js_context_get_field(
      ctx, global, neo_js_context_create_string(ctx, "Intl"), NULL);
  NEO_JS_TRY_AND_THROW(intl);
  neo_js_variable_t collator = neo_js_context_get_field(
      ctx, intl, neo_js_context_create_string(ctx, "Collator"), NULL);
  NEO_JS_TRY_AND_THROW(collator);
  neo_js_variable_t ins = neo_js_context_construct(ctx, collator, 1, &self);
  NEO_JS_TRY_AND_THROW(ins);
  neo_js_variable_t compare = neo_js_context_get_field(
      ctx, ins, neo_js_context_create_string(ctx, "compare"), NULL);
  NEO_JS_TRY_AND_THROW(compare);
  return neo_js_context_call(ctx, compare, ins, argc, argv);
}
NEO_JS_CFUNCTION(neo_js_string_match) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.match called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  neo_js_variable_t match = NULL;
  neo_js_variable_t symbol = neo_js_context_get_std(ctx).symbol_constructor;
  symbol = neo_js_context_get_field(
      ctx, symbol, neo_js_context_create_string(ctx, "match"), NULL);
  NEO_JS_TRY_AND_THROW(symbol);
  if (!argc) {
    match = neo_js_context_create_string(ctx, "/(?:)/");
  } else {
    match = argv[0];
  }
  if (!neo_js_context_has_field(ctx, match, symbol)) {
    match = neo_js_context_construct(
        ctx, neo_js_context_get_std(ctx).regexp_constructor, 1, &match);
    NEO_JS_TRY_AND_THROW(match);
  }
  neo_js_variable_t match_func =
      neo_js_context_get_field(ctx, match, symbol, NULL);
  return neo_js_context_call(ctx, match_func, match, 1, &self);
}
NEO_JS_CFUNCTION(neo_js_string_match_all) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.matchAll called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  neo_js_variable_t match = NULL;
  neo_js_variable_t symbol = neo_js_context_get_std(ctx).symbol_constructor;
  symbol = neo_js_context_get_field(
      ctx, symbol, neo_js_context_create_string(ctx, "matchAll"), NULL);
  NEO_JS_TRY_AND_THROW(symbol);
  if (!argc) {
    match = neo_js_context_create_string(ctx, "/(?:)/g");
  } else {
    match = argv[0];
  }
  if (!neo_js_context_has_field(ctx, match, symbol)) {
    match = neo_js_context_construct(
        ctx, neo_js_context_get_std(ctx).regexp_constructor, 1, &match);
    NEO_JS_TRY_AND_THROW(match);
  }
  if (neo_js_variable_get_type(match)->kind == NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t regexp = neo_js_context_get_std(ctx).regexp_constructor;
    neo_js_variable_t res = neo_js_context_instance_of(ctx, match, regexp);
    NEO_JS_TRY_AND_THROW(res);
    if (neo_js_variable_to_boolean(res)->boolean) {
      if (!(neo_js_regexp_get_flag(ctx, match) & NEO_REGEXP_FLAG_GLOBAL)) {
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_TYPE, 0,
            "String.prototype.matchAll called with a non-global RegExp "
            "argument");
      }
    }
  }
  neo_js_variable_t func = neo_js_context_get_field(ctx, match, symbol, NULL);
  NEO_JS_TRY_AND_THROW(func);
  return neo_js_context_call(ctx, func, match, 1, &self);
}
NEO_JS_CFUNCTION(neo_js_string_normalize) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.normalized called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  return self;
}
NEO_JS_CFUNCTION(neo_js_string_pad_end) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.padEnd called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  neo_js_string_t source = neo_js_variable_to_string(self);
  size_t len = neo_string16_length(source->string);
  int64_t newlen = 0;
  if (argc) {
    neo_js_variable_t vlen = neo_js_context_to_integer(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(vlen);
    newlen = neo_js_variable_to_number(vlen)->number;
  }
  if (newlen <= len) {
    return self;
  }
  uint16_t pad_empty[] = {' ', 0};
  const uint16_t *pad = pad_empty;
  if (argc > 1) {
    neo_js_variable_t vpad = neo_js_context_to_string(ctx, argv[1]);
    NEO_JS_TRY_AND_THROW(vpad);
    pad = neo_js_variable_to_string(vpad)->string;
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  uint16_t *res =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * (newlen + 1), NULL);
  res[newlen] = 0;
  memcpy(res, source->string, len * sizeof(uint16_t));
  size_t idx = 0;
  for (size_t offset = len; offset < newlen; offset++) {
    res[offset] = pad[idx];
    idx++;
    if (pad[idx] == 0) {
      idx = 0;
    }
  }
  neo_js_variable_t result = neo_js_context_create_string(ctx, "");
  neo_js_string_t target = neo_js_variable_to_string(result);
  neo_allocator_free(allocator, target->string);
  target->string = res;
  return result;
}
NEO_JS_CFUNCTION(neo_js_string_pad_start) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.padStart called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  neo_js_string_t source = neo_js_variable_to_string(self);
  size_t len = neo_string16_length(source->string);
  int64_t newlen = 0;
  if (argc) {
    neo_js_variable_t vlen = neo_js_context_to_integer(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(vlen);
    newlen = neo_js_variable_to_number(vlen)->number;
  }
  if (newlen <= len) {
    return self;
  }
  uint16_t pad_empty[] = {' ', 0};
  const uint16_t *pad = pad_empty;
  if (argc > 1) {
    neo_js_variable_t vpad = neo_js_context_to_string(ctx, argv[1]);
    NEO_JS_TRY_AND_THROW(vpad);
    pad = neo_js_variable_to_string(vpad)->string;
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  uint16_t *res =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * (newlen + 1), NULL);
  res[newlen] = 0;
  size_t position = newlen - len;
  memcpy(res + position, source->string, len * sizeof(uint16_t));
  size_t idx = 0;
  for (size_t offset = 0; offset < position; offset++) {
    res[offset] = pad[idx];
    idx++;
    if (pad[idx] == 0) {
      idx = 0;
    }
  }
  neo_js_variable_t result = neo_js_context_create_string(ctx, "");
  neo_js_string_t target = neo_js_variable_to_string(result);
  neo_allocator_free(allocator, target->string);
  target->string = res;
  return result;
}
NEO_JS_CFUNCTION(neo_js_string_repeat) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.repeat called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  int64_t count = 0;
  if (argc) {
    neo_js_variable_t vcount = neo_js_context_to_integer(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(vcount);
    count = neo_js_variable_to_number(vcount)->number;
    if (count < 0 || count >= NEO_MAX_INTEGER) {
      return neo_js_context_create_simple_error(
          ctx, NEO_JS_ERROR_RANGE, 0, "Invalid count value: %ld", count);
    }
  }
  const uint16_t *source = neo_js_variable_to_string(self)->string;
  size_t len = neo_string16_length(source);
  size_t result_len = len * count;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  uint16_t *dst =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * (result_len + 1), NULL);
  for (size_t offset = 0; offset < count; offset++) {
    memcpy(dst + offset * len, source, len * sizeof(uint16_t));
  }
  dst[result_len] = 0;
  neo_js_variable_t result = neo_js_context_create_string(ctx, "");
  neo_js_string_t target = neo_js_variable_to_string(result);
  neo_allocator_free(allocator, target->string);
  target->string = dst;
  return result;
}
NEO_JS_CFUNCTION(neo_js_string_replace) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.replace called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  const uint16_t *str = neo_js_variable_to_string(self)->string;
  size_t len = neo_string16_length(str);
  neo_js_variable_t replace = NULL;
  neo_js_variable_t symbol = neo_js_context_get_std(ctx).symbol_constructor;
  symbol = neo_js_context_get_field(
      ctx, symbol, neo_js_context_create_string(ctx, "replace"), NULL);
  NEO_JS_TRY_AND_THROW(symbol);
  if (!argc) {
    replace = neo_js_context_create_string(ctx, "");
  } else {
    replace = argv[0];
  }
  neo_js_variable_t replacement = NULL;
  if (argc > 1) {
    replacement = argv[1];
    if (neo_js_variable_get_type(replacement)->kind >= NEO_JS_TYPE_CALLABLE) {
      replacement = neo_js_context_to_string(ctx, replacement);
      NEO_JS_TRY_AND_THROW(replacement);
    }
  } else {
    replacement = neo_js_context_create_string(ctx, "undefined");
  }
  if (neo_js_context_has_field(ctx, replace, symbol)) {
    neo_js_variable_t replace_func =
        neo_js_context_get_field(ctx, replace, symbol, NULL);
    if (argc > 1) {
      neo_js_variable_t args[] = {self, argv[1]};
      return neo_js_context_call(ctx, replace_func, replace, 2, args);
    } else {
      return neo_js_context_call(ctx, replace_func, replace, 1, &self);
    }
  } else {
    replace = neo_js_context_to_string(ctx, replace);
    NEO_JS_TRY_AND_THROW(replace);
    const uint16_t *prep = neo_js_variable_to_string(replace)->string;
    if (prep[0] == 0) {
      if (neo_js_variable_get_type(replacement)->kind >= NEO_JS_TYPE_CALLABLE) {
        neo_js_variable_t args[] = {
            neo_js_context_create_string(ctx, ""),
            neo_js_context_create_number(ctx, 0),
            self,
        };
        replacement = neo_js_context_call(
            ctx, replacement, neo_js_context_create_undefined(ctx), 3, args);
        NEO_JS_TRY_AND_THROW(replacement);
        replacement = neo_js_context_to_string(ctx, replacement);
        NEO_JS_TRY_AND_THROW(replacement);
      }
      const uint16_t *newstr = neo_js_variable_to_string(replacement)->string;
      len = neo_string16_length(newstr);
      uint16_t buf[len + 1];
      buf[len] = 0;
      uint16_t *dst = &buf[0];
      while (*newstr) {
        *dst = *newstr;
        dst++;
        newstr++;
      }
      while (*str) {
        *dst = *str;
        str++;
        dst++;
      }
      return neo_js_context_create_string16(ctx, dst);
    }
    int64_t idx = neo_string16_find(str, prep);
    if (idx == -1) {
      return neo_js_context_create_string16(ctx, str);
    }
    int64_t partStart = idx;
    int64_t partEnd = partStart + neo_string16_length(prep);
    size_t originLen = partEnd - partStart;
    if (neo_js_variable_get_type(replacement)->kind >= NEO_JS_TYPE_CALLABLE) {
      uint16_t origin[originLen + 1];
      origin[originLen] = 0;
      for (int64_t idx = partStart; idx < partEnd; idx++) {
        origin[idx - partStart] = str[idx];
      }
      neo_js_variable_t offset = neo_js_context_create_number(ctx, partStart);
      neo_js_variable_t args[] = {replace, offset, self};
      replacement = neo_js_context_call(
          ctx, replacement, neo_js_context_create_undefined(ctx), 3, args);
      NEO_JS_TRY_AND_THROW(replacement);
      replacement = neo_js_context_to_string(ctx, replacement);
      NEO_JS_TRY_AND_THROW(replacement);
    }
    uint16_t *newstr = neo_js_variable_to_string(replacement)->string;
    len -= originLen + neo_string16_length(newstr);
    uint16_t buf[len + 1];
    buf[len] = 0;
    for (idx = 0; idx < partStart; idx++) {
      buf[idx] = str[idx];
    }
    while (*newstr) {
      buf[idx] = *newstr;
      newstr++;
      idx++;
    }
    while (str[partEnd]) {
      buf[idx] = str[partEnd];
      partEnd++;
      idx++;
    }
    return neo_js_context_create_string16(ctx, buf);
  }
}
NEO_JS_CFUNCTION(neo_js_string_replace_all) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.replaceAll called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  const uint16_t *str = neo_js_variable_to_string(self)->string;
  size_t len = neo_string16_length(str);
  neo_js_variable_t replace = NULL;
  neo_js_variable_t symbol = neo_js_context_get_std(ctx).symbol_constructor;
  symbol = neo_js_context_get_field(
      ctx, symbol, neo_js_context_create_string(ctx, "replaceAll"), NULL);
  NEO_JS_TRY_AND_THROW(symbol);
  if (!argc) {
    replace = neo_js_context_create_string(ctx, "");
  } else {
    replace = argv[0];
  }
  neo_js_variable_t replacement = NULL;
  if (argc > 1) {
    replacement = argv[1];
    if (neo_js_variable_get_type(replacement)->kind >= NEO_JS_TYPE_CALLABLE) {
      replacement = neo_js_context_to_string(ctx, replacement);
      NEO_JS_TRY_AND_THROW(replacement);
    }
  } else {
    replacement = neo_js_context_create_string(ctx, "undefined");
  }
  if (neo_js_context_has_field(ctx, replace, symbol)) {
    if (neo_js_variable_get_type(replace)->kind == NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t regexp = neo_js_context_get_std(ctx).regexp_constructor;
      neo_js_variable_t res = neo_js_context_instance_of(ctx, replace, regexp);
      NEO_JS_TRY_AND_THROW(res);
      if (neo_js_variable_to_boolean(res)->boolean) {
        if (!(neo_js_regexp_get_flag(ctx, replace) & NEO_REGEXP_FLAG_GLOBAL)) {
          return neo_js_context_create_simple_error(
              ctx, NEO_JS_ERROR_TYPE, 0,
              "String.prototype.replaceAll called with a non-global RegExp "
              "argument");
        }
      }
    }
    neo_js_variable_t replace_func =
        neo_js_context_get_field(ctx, replace, symbol, NULL);
    if (argc > 1) {
      neo_js_variable_t args[] = {self, argv[1]};
      return neo_js_context_call(ctx, replace_func, replace, 2, args);
    } else {
      return neo_js_context_call(ctx, replace_func, replace, 1, &self);
    }
  } else {
    replace = neo_js_context_to_string(ctx, replace);
    NEO_JS_TRY_AND_THROW(replace);
    const uint16_t *prep = neo_js_variable_to_string(replace)->string;
    if (prep[0] == 0) {
      neo_js_variable_t res = neo_js_context_create_string(ctx, "");
      size_t idx = 0;
      for (idx = 0; idx < len; idx++) {
        neo_js_variable_t r = replacement;
        if (neo_js_variable_get_type(r)->kind >= NEO_JS_TYPE_CALLABLE) {
          neo_js_variable_t args[] = {
              replace,
              neo_js_context_create_number(ctx, idx),
              self,
          };
          r = neo_js_context_call(ctx, r, neo_js_context_create_undefined(ctx),
                                  3, args);
          NEO_JS_TRY_AND_THROW(r);
          r = neo_js_context_to_string(ctx, r);
          NEO_JS_TRY_AND_THROW(r);
        }
        res = neo_js_context_concat(ctx, res, r);
        uint16_t s[] = {str[idx], 0};
        res = neo_js_context_concat(ctx, res,
                                    neo_js_context_create_string16(ctx, s));
      }
      neo_js_variable_t r = replacement;
      if (neo_js_variable_get_type(r)->kind >= NEO_JS_TYPE_CALLABLE) {
        neo_js_variable_t args[] = {
            replace,
            neo_js_context_create_number(ctx, idx),
            self,
        };
        r = neo_js_context_call(ctx, r, neo_js_context_create_undefined(ctx), 3,
                                args);
        NEO_JS_TRY_AND_THROW(r);
        r = neo_js_context_to_string(ctx, r);
        NEO_JS_TRY_AND_THROW(r);
      }
      res = neo_js_context_concat(ctx, res, r);
      return res;
    }
    int64_t idx = neo_string16_find(str, prep);
    neo_js_variable_t res = neo_js_context_create_string(ctx, "");
    int64_t start = 0;
    int64_t end = idx;
    while (idx != -1) {
      end = idx;
      int64_t len = end - start;
      uint16_t buf[len + 1];
      buf[len] = 0;
      for (int64_t offset = start; offset != end; offset++) {
        buf[offset - start] = str[offset];
      }
      res = neo_js_context_concat(ctx, res,
                                  neo_js_context_create_string16(ctx, buf));
      neo_js_variable_t r = replacement;
      if (neo_js_variable_get_type(r)->kind >= NEO_JS_TYPE_CALLABLE) {
        neo_js_variable_t args[] = {
            replace, neo_js_context_create_number(ctx, end), self};
        r = neo_js_context_call(ctx, r, neo_js_context_create_undefined(ctx), 3,
                                args);
        NEO_JS_TRY_AND_THROW(r);
        r = neo_js_context_to_string(ctx, r);
        NEO_JS_TRY_AND_THROW(r);
      }
      res = neo_js_context_concat(ctx, res, r);
      start = end;
      idx = neo_string16_find(&str[start], prep);
    }
    res = neo_js_context_concat(
        ctx, res, neo_js_context_create_string16(ctx, &str[start]));
    return res;
  }
}
NEO_JS_CFUNCTION(neo_js_string_search) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.search called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  neo_js_variable_t search = NULL;
  neo_js_variable_t symbol = neo_js_context_get_std(ctx).symbol_constructor;
  symbol = neo_js_context_get_field(
      ctx, symbol, neo_js_context_create_string(ctx, "search"), NULL);
  NEO_JS_TRY_AND_THROW(symbol);
  if (!argc) {
    search = neo_js_context_create_string(ctx, "");
  } else {
    search = argv[0];
  }
  if (!neo_js_context_has_field(ctx, search, symbol)) {
    search = neo_js_context_construct(
        ctx, neo_js_context_get_std(ctx).regexp_constructor, 1, &search);
    NEO_JS_TRY_AND_THROW(search);
  }
  neo_js_variable_t search_func =
      neo_js_context_get_field(ctx, search, symbol, NULL);
  return neo_js_context_call(ctx, search_func, search, 1, &self);
}
NEO_JS_CFUNCTION(neo_js_string_slice) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.slice called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  const uint16_t *str = neo_js_variable_to_string(self)->string;
  int64_t len = neo_string16_length(str);
  int64_t indexStart = 0;
  int64_t indexEnd = 0;
  if (argc) {
    neo_js_variable_t vstart = neo_js_context_to_integer(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(vstart);
    indexStart = neo_js_variable_to_number(vstart)->number;
  }
  if (argc > 1) {
    neo_js_variable_t vend = neo_js_context_to_integer(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(vend);
    indexEnd = neo_js_variable_to_number(vend)->number;
  } else {
    indexEnd = len;
  }
  if (indexStart >= len) {
    return neo_js_context_create_string(ctx, "");
  }
  if (indexStart < 0) {
    indexStart += len;
    if (indexStart < 0) {
      indexStart = 0;
    }
  }
  if (indexEnd < 0) {
    indexEnd += len;
    if (indexEnd < 0) {
      indexEnd = 0;
    }
  }
  if (indexEnd <= indexStart) {
    return neo_js_context_create_string(ctx, "");
  }
  len = indexEnd - indexStart;
  uint16_t buf[len];
  buf[len - 1] = 0;
  for (int64_t idx = indexStart; idx < indexEnd; idx++) {
    buf[idx - indexStart] = str[idx];
  }
  return neo_js_context_create_string16(ctx, buf);
}
NEO_JS_CFUNCTION(neo_js_string_split) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.split called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  const uint16_t *str = neo_js_variable_to_string(self)->string;
  neo_js_variable_t result = neo_js_context_create_array(ctx);
  if (!argc) {
    NEO_JS_TRY_AND_THROW(neo_js_array_push(ctx, result, 1, &self));
    return result;
  }
  const uint16_t *spliter = NULL;
  neo_js_variable_t vspliter = neo_js_context_to_string(ctx, argv[0]);
  NEO_JS_TRY_AND_THROW(vspliter);
  spliter = neo_js_variable_to_string(vspliter)->string;
  int64_t limits = 0;
  if (argc > 1) {
    neo_js_variable_t vlimits = neo_js_context_to_integer(ctx, argv[1]);
    limits = neo_js_variable_to_number(vlimits)->number;
    if (!limits) {
      return result;
    }
  }
  int64_t len = 0;
  int64_t start = 0;
  int64_t end = 0;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  while (str[end] && (len < limits || limits == 0)) {
    if (spliter[0] == 0) {
      uint16_t part[] = {str[start], 0};
      neo_js_variable_t item = neo_js_context_create_string16(ctx, part);
      NEO_JS_TRY_AND_THROW(neo_js_array_push(ctx, result, 1, &item));
      len++;
      end++;
      start++;
    } else {
      const uint16_t *p = spliter;
      int64_t offset = 0;
      while (*p) {
        if (str[offset + end] != *p) {
          break;
        }
        p++;
        offset++;
      }
      if (!*p) {
        size_t sublen = end - start;
        uint16_t part[sublen];
        part[sublen - 1] = 0;
        for (int64_t idx = start; idx < end; idx++) {
          part[idx - start] = str[idx];
        }
        neo_js_variable_t item = neo_js_context_create_string16(ctx, part);
        NEO_JS_TRY_AND_THROW(neo_js_array_push(ctx, result, 1, &item));
        len++;
        end = offset;
        start = offset;
      } else {
        end++;
      }
    }
  }
  if (len < limits || limits == 0) {
    neo_js_variable_t item = neo_js_context_create_string16(ctx, &str[start]);
    NEO_JS_TRY_AND_THROW(neo_js_array_push(ctx, result, 1, &item));
  }
  return result;
}
NEO_JS_CFUNCTION(neo_js_string_starts_with) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.startsWith called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  const uint16_t *str = neo_js_variable_to_string(self)->string;
  neo_js_variable_t vsearch = NULL;
  if (argc) {
    if (neo_js_variable_get_type(argv[0])->kind == NEO_JS_TYPE_OBJECT) {
      neo_js_variable_t regexp_constructor =
          neo_js_context_get_std(ctx).regexp_constructor;
      neo_js_variable_t res =
          neo_js_context_instance_of(ctx, argv[0], regexp_constructor);
      NEO_JS_TRY_AND_THROW(res);
      if (neo_js_variable_to_boolean(res)->boolean) {
        return neo_js_context_create_simple_error(
            ctx, NEO_JS_ERROR_TYPE, 0,
            "First argument to String.prototype.endsWith must not be a regular "
            "expression");
      }
    }
    vsearch = neo_js_context_to_string(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(vsearch);
  } else {
    vsearch = neo_js_context_create_string(ctx, "undefined");
  }
  const uint16_t *search = neo_js_variable_to_string(vsearch)->string;
  while (*search && *str) {
    if (*search != *str) {
      break;
    }
    search++;
    str++;
  }
  return neo_js_context_create_boolean(ctx, *search == *str);
}
NEO_JS_CFUNCTION(neo_js_string_substring) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.substring called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  const uint16_t *str = neo_js_variable_to_string(self)->string;
  int64_t len = neo_string16_length(str);
  int64_t indexStart = 0;
  int64_t indexEnd = 0;
  if (argc) {
    neo_js_variable_t vstart = neo_js_context_to_integer(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(vstart);
    indexStart = neo_js_variable_to_number(vstart)->number;
  }
  if (argc > 1) {
    neo_js_variable_t vend = neo_js_context_to_integer(ctx, argv[0]);
    NEO_JS_TRY_AND_THROW(vend);
    indexEnd = neo_js_variable_to_number(vend)->number;
  } else {
    indexEnd = len;
  }
  if (indexStart < 0) {
    indexStart = 0;
  }
  if (indexStart > len) {
    indexStart = len;
  }
  if (indexEnd < 0) {
    indexEnd = 0;
  }
  if (indexEnd > len) {
    indexEnd = len;
  }
  if (indexStart > indexEnd) {
    int64_t tmp = indexStart;
    indexStart = indexEnd;
    indexEnd = tmp;
  }
  if (indexStart == len || indexStart == indexEnd) {
    return neo_js_context_create_string(ctx, "");
  }
  len = indexEnd - indexStart;
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  uint16_t *res = neo_allocator_alloc(allocator, len, NULL);
  res[len] = 0;
  for (int64_t idx = indexStart; idx < indexEnd; idx++) {
    res[idx - indexStart] = str[indexStart];
  }
  neo_js_variable_t result = neo_js_context_create_string(ctx, "");
  neo_js_string_t string = neo_js_variable_to_string(result);
  neo_allocator_free(allocator, string->string);
  string->string = res;
  return result;
}
NEO_JS_CFUNCTION(neo_js_string_to_local_lower_case) {
  return neo_js_string_to_lower_case(ctx, self, argc, argv);
}
NEO_JS_CFUNCTION(neo_js_string_to_local_upper_case) {
  return neo_js_string_to_upper_case(ctx, self, argc, argv);
}
NEO_JS_CFUNCTION(neo_js_string_to_lower_case) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.toLowerCase called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  const uint16_t *str = neo_js_variable_to_string(self)->string;
  size_t len = neo_string16_length(str);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  uint16_t *res =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * (len + 1), NULL);
  res[len] = 0;
  for (size_t idx = 0; idx < len; idx++) {
    if (str[idx] >= 'A' && str[idx] <= 'A') {
      res[idx] = str[idx] + 'a' - 'A';
    } else {
      res[idx] = str[idx];
    }
    idx++;
  }
  neo_js_variable_t result = neo_js_context_create_string(ctx, "");
  neo_js_string_t string = neo_js_variable_to_string(result);
  neo_allocator_free(allocator, string->string);
  string->string = res;
  return result;
}
NEO_JS_CFUNCTION(neo_js_string_to_string) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.toString called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  return self;
}
NEO_JS_CFUNCTION(neo_js_string_to_upper_case) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.toUpperCase called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  const uint16_t *str = neo_js_variable_to_string(self)->string;
  size_t len = neo_string16_length(str);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  uint16_t *res =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * (len + 1), NULL);
  res[len] = 0;
  for (size_t idx = 0; idx < len; idx++) {
    if (str[idx] >= 'a' && str[idx] <= 'z') {
      res[idx] = str[idx] + 'A' - 'a';
    } else {
      res[idx] = str[idx];
    }
    idx++;
  }
  neo_js_variable_t result = neo_js_context_create_string(ctx, "");
  neo_js_string_t string = neo_js_variable_to_string(result);
  neo_allocator_free(allocator, string->string);
  string->string = res;
  return result;
}
NEO_JS_CFUNCTION(neo_js_string_to_well_formed) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.trim called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  const uint16_t *str = neo_js_variable_to_string(self)->string;
  size_t len = neo_string16_length(str);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  uint16_t *res =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * (len + 1), NULL);
  res[len] = 0;
  uint16_t *pres = res;
  while (*str) {
    if (*str <= 0xd800) {
      *pres = *str;
      str++;
      pres++;
    } else if (*str > 0xd800 && *str <= 0xdbff) {
      if (*(str + 1) > 0xdc00 && *(str + 1) <= 0xdfff) {
        *pres = *str;
        pres++;
        str++;
        *pres = *str;
        pres++;
        str++;
      } else {
        str++;
        *pres = 0xfffd;
      }
    } else if (*str > 0xdc00 && *str <= 0xdfff) {
      str++;
      *pres = 0xfffd;
    }
  }
  neo_js_variable_t result = neo_js_context_create_string(ctx, "");
  neo_js_string_t string = neo_js_variable_to_string(result);
  neo_allocator_free(allocator, string->string);
  string->string = res;
  return result;
}
NEO_JS_CFUNCTION(neo_js_string_trim) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.trim called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  const uint16_t *str = neo_js_variable_to_string(self)->string;
  while (IS_SPACE_SEPARATOR(*str) || *str == 0x2028 || *str == 0x2029 ||
         *str == '\n' || *str == '\r') {
    str++;
  }
  size_t len = neo_string16_length(str);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  uint16_t *res =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * (len + 1), NULL);
  memcpy(res, str, sizeof(uint16_t) * len);
  res[len] = 0;
  len--;
  while (len > 0 &&
         (IS_SPACE_SEPARATOR(res[len]) || res[len] == 0x2028 ||
          res[len] == 0x2029 || res[len] == '\n' || res[len] == '\r')) {
    res[len] = 0;
    len--;
  }
  neo_js_variable_t result = neo_js_context_create_string(ctx, "");
  neo_js_string_t string = neo_js_variable_to_string(result);
  neo_allocator_free(allocator, string->string);
  string->string = res;
  return result;
}
NEO_JS_CFUNCTION(neo_js_string_trim_end) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.trimEnd called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  const uint16_t *str = neo_js_variable_to_string(self)->string;
  size_t len = neo_string16_length(str);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  uint16_t *res =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * (len + 1), NULL);
  memcpy(res, str, sizeof(uint16_t) * len);
  res[len] = 0;
  len--;
  while (len > 0 &&
         (IS_SPACE_SEPARATOR(res[len]) || res[len] == 0x2028 ||
          res[len] == 0x2029 || res[len] == '\n' || res[len] == '\r')) {
    res[len] = 0;
    len--;
  }
  neo_js_variable_t result = neo_js_context_create_string(ctx, "");
  neo_js_string_t string = neo_js_variable_to_string(result);
  neo_allocator_free(allocator, string->string);
  string->string = res;
  return result;
}
NEO_JS_CFUNCTION(neo_js_string_trim_start) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.trimStart called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  const uint16_t *str = neo_js_variable_to_string(self)->string;
  while (IS_SPACE_SEPARATOR(*str) || *str == 0x2028 || *str == 0x2029 ||
         *str == '\n' || *str == '\r') {
    str++;
  }
  size_t len = neo_string16_length(str);
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  uint16_t *res =
      neo_allocator_alloc(allocator, sizeof(uint16_t) * (len + 1), NULL);
  memcpy(res, str, sizeof(uint16_t) * len);
  res[len] = 0;
  neo_js_variable_t result = neo_js_context_create_string(ctx, "");
  neo_js_string_t string = neo_js_variable_to_string(result);
  neo_allocator_free(allocator, string->string);
  string->string = res;
  return result;
}
NEO_JS_CFUNCTION(neo_js_string_value_of) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.trimStart called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  return self;
}
static NEO_JS_CFUNCTION(neo_js_string_iterator_next) {
  neo_js_variable_t string =
      neo_js_context_get_internal(ctx, self, "[[string]]");
  neo_js_variable_t index = neo_js_context_get_internal(ctx, self, "[[index]]");
  if (!string || !index ||
      neo_js_variable_get_type(string)->kind != NEO_JS_TYPE_STRING ||
      neo_js_variable_get_type(index)->kind != NEO_JS_TYPE_NUMBER) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "Failed to call next without StringIterator");
  }
  int64_t idx = neo_js_variable_to_number(index)->number;
  const uint16_t *str = neo_js_variable_to_string(string)->string;
  neo_js_variable_t result = neo_js_context_create_object(ctx, NULL);
  if (!str[idx]) {
    neo_js_context_set_field(ctx, result,
                             neo_js_context_create_string(ctx, "done"),
                             neo_js_context_create_boolean(ctx, true), NULL);
    return result;
  }
  uint16_t buf[3];
  if (str[idx] <= 0xd800) {
    buf[0] = str[idx];
    buf[1] = 0;
    idx++;
  } else if (str[idx] > 0xd800 && str[idx] <= 0xdbff) {
    buf[0] = str[idx];
    idx++;
    if (str[idx] > 0xdc00 && str[idx] <= 0xdfff) {
      buf[1] = str[idx];
      buf[2] = 0;
      idx++;
    } else {
      buf[1] = 0;
    }
  } else if (str[idx] > 0xdc00 && str[idx] <= 0xdfff) {
    buf[0] = str[idx];
    idx++;
    buf[1] = 0;
  }
  neo_js_variable_to_number(index)->number = idx;
  return neo_js_context_create_string16(ctx, buf);
}
static NEO_JS_CFUNCTION(neo_js_string_iterator_iterator) { return self; }
NEO_JS_CFUNCTION(neo_js_string_iterator) {
  if (neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_NULL ||
      neo_js_variable_get_type(self)->kind == NEO_JS_TYPE_UNDEFINED) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE, 0,
        "String.prototype.[Symbol.iterator] called on null or undefined");
  }
  if (neo_js_variable_get_type(self)->kind != NEO_JS_TYPE_STRING) {
    self = neo_js_context_to_string(ctx, self);
    NEO_JS_TRY_AND_THROW(self);
  }
  neo_js_variable_t iterator = neo_js_context_create_object(ctx, NULL);
  neo_js_context_set_internal(ctx, iterator, "[[string]]", self);
  neo_js_context_set_internal(ctx, iterator, "[[index]]",
                              neo_js_context_create_number(ctx, 0));
  NEO_JS_SET_METHOD(ctx, iterator, "next", neo_js_string_iterator_next);
  NEO_JS_SET_SYMBOL_METHOD(ctx, iterator, "iterator",
                           neo_js_string_iterator_iterator);
  neo_js_context_set_field(
      ctx, iterator,
      neo_js_context_get_field(
          ctx, neo_js_context_get_std(ctx).symbol_constructor,
          neo_js_context_create_string(ctx, "StringTag"), NULL),
      neo_js_context_create_string(ctx, "Symbol Iterator"), NULL);
  return iterator;
}
void neo_js_context_init_std_string(neo_js_context_t ctx) {
  neo_js_variable_t constructor =
      neo_js_context_get_std(ctx).string_constructor;
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, constructor, neo_js_context_create_string(ctx, "prototype"), NULL);
  neo_js_variable_t global = neo_js_context_get_global(ctx);
  neo_js_context_set_field(ctx, global,
                           neo_js_context_create_string(ctx, "String"),
                           constructor, NULL);
  NEO_JS_SET_METHOD(ctx, constructor, "fromCharCode",
                    neo_js_string_from_char_code);
  NEO_JS_SET_METHOD(ctx, constructor, "fromCodePoint",
                    neo_js_string_from_code_point);
  NEO_JS_SET_METHOD(ctx, constructor, "raw", neo_js_string_raw);

  NEO_JS_SET_METHOD(ctx, prototype, "at", neo_js_string_at);
  NEO_JS_SET_METHOD(ctx, prototype, "charAt", neo_js_string_char_at);
  NEO_JS_SET_METHOD(ctx, prototype, "charCodeAt", neo_js_string_char_code_at);
  NEO_JS_SET_METHOD(ctx, prototype, "codePointAt", neo_js_string_code_point_at);
  NEO_JS_SET_METHOD(ctx, prototype, "concat", neo_js_string_concat);
  NEO_JS_SET_METHOD(ctx, prototype, "endsWith", neo_js_string_ends_with);
  NEO_JS_SET_METHOD(ctx, prototype, "includes", neo_js_string_includes);
  NEO_JS_SET_METHOD(ctx, prototype, "indexOf", neo_js_string_index_of);
  NEO_JS_SET_METHOD(ctx, prototype, "isWellFormed",
                    neo_js_string_is_well_formed);
  NEO_JS_SET_METHOD(ctx, prototype, "lastIndexOf", neo_js_string_last_index_of);
  NEO_JS_SET_METHOD(ctx, prototype, "localCompare",
                    neo_js_string_local_compare);
  NEO_JS_SET_METHOD(ctx, prototype, "match", neo_js_string_match);
  NEO_JS_SET_METHOD(ctx, prototype, "matchAll", neo_js_string_match_all);
  NEO_JS_SET_METHOD(ctx, prototype, "normalize", neo_js_string_normalize);
  NEO_JS_SET_METHOD(ctx, prototype, "padEnd", neo_js_string_pad_end);
  NEO_JS_SET_METHOD(ctx, prototype, "padStart", neo_js_string_pad_start);
  NEO_JS_SET_METHOD(ctx, prototype, "repeat", neo_js_string_repeat);
  NEO_JS_SET_METHOD(ctx, prototype, "replace", neo_js_string_replace);
  NEO_JS_SET_METHOD(ctx, prototype, "replaceAll", neo_js_string_replace_all);
  NEO_JS_SET_METHOD(ctx, prototype, "search", neo_js_string_search);
  NEO_JS_SET_METHOD(ctx, prototype, "slice", neo_js_string_slice);
  NEO_JS_SET_METHOD(ctx, prototype, "split", neo_js_string_split);
  NEO_JS_SET_METHOD(ctx, prototype, "startsWith", neo_js_string_starts_with);
  NEO_JS_SET_METHOD(ctx, prototype, "substring", neo_js_string_substring);
  NEO_JS_SET_METHOD(ctx, prototype, "toLocalLowerCase",
                    neo_js_string_to_local_lower_case);
  NEO_JS_SET_METHOD(ctx, prototype, "toLocalUpperCase",
                    neo_js_string_to_local_upper_case);
  NEO_JS_SET_METHOD(ctx, prototype, "toLowerCase", neo_js_string_to_lower_case);
  NEO_JS_SET_METHOD(ctx, prototype, "toString", neo_js_string_to_string);
  NEO_JS_SET_METHOD(ctx, prototype, "toUpperCase", neo_js_string_to_upper_case);
  NEO_JS_SET_METHOD(ctx, prototype, "toWellFormed",
                    neo_js_string_to_well_formed);
  NEO_JS_SET_METHOD(ctx, prototype, "trim", neo_js_string_trim);
  NEO_JS_SET_METHOD(ctx, prototype, "trimEnd", neo_js_string_trim_end);
  NEO_JS_SET_METHOD(ctx, prototype, "trimStart", neo_js_string_trim_start);
  NEO_JS_SET_METHOD(ctx, prototype, "valueOf", neo_js_string_value_of);
  neo_js_variable_t symbol = neo_js_context_get_std(ctx).symbol_constructor;
  symbol = neo_js_context_get_field(
      ctx, symbol, neo_js_context_create_string(ctx, "iterator"), NULL);
  neo_js_context_set_field(
      ctx, prototype, symbol,
      neo_js_context_create_cfunction(ctx, "[Symbol.iterator]",
                                      neo_js_string_iterator_next),
      NULL);
}