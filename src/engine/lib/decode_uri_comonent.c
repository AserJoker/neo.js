#include "core/unicode.h"
#include "engine/context.h"
#include "engine/lib/decode_uri_component.h"
#include <wchar.h>

NEO_JS_CFUNCTION(neo_js_decode_uri_component) {
  neo_js_variable_t str = NULL;
  if (argc) {
    str = argv[0];
  } else {
    str = neo_js_context_create_undefined(ctx);
  }
  str = neo_js_context_to_string(ctx, str);
  NEO_JS_TRY_AND_THROW(str);
  const wchar_t *source = neo_js_variable_to_string(str)->string;
  size_t len = wcslen(source);
  uint8_t *utf8_string = neo_js_context_alloc(ctx, len + 1, NULL);
  neo_js_context_defer_free(ctx, utf8_string);
  uint8_t *pdst = utf8_string;
  const wchar_t *psrc = source;
  while (*psrc) {
    if (*psrc == '%') {
      psrc++;
      *pdst = 0;
      for (uint8_t idx = 0; idx < 2; idx++) {
        if (*psrc >= '0' && *psrc <= '9') {
          *pdst += *psrc - '0';
        } else if (*psrc >= 'a' && *psrc <= 'f') {
          *pdst += *psrc - 'a' + 10;
        } else if (*psrc >= 'A' && *psrc <= 'F') {
          *pdst += *psrc - 'A' + 10;
        } else {
          return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_URI,
                                                    L"malformed URI sequence");
        }
        psrc++;
        if (idx == 0) {
          *pdst *= 16;
        }
      }
      pdst++;
    } else {
      *pdst++ = *psrc++;
    }
  }
  wchar_t *dst = neo_string_to_wstring(neo_js_context_get_allocator(ctx),
                                       (char *)utf8_string);
  neo_js_context_defer_free(ctx, dst);
  return neo_js_context_create_string(ctx, dst);
}