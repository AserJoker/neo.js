
#include "core/unicode.h"
#include "engine/context.h"
NEO_JS_CFUNCTION(neo_js_encode_uri_component) {
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
  wchar_t *result =
      neo_js_context_alloc(ctx, sizeof(wchar_t) * (len * 6 + 1), NULL);
  neo_js_context_defer_free(ctx, result);
  const wchar_t *psrc = source;
  wchar_t *pdst = result;
  while (*psrc) {
    if (*psrc >= 'a' && *psrc <= 'z') {
      *pdst++ = *psrc++;
    } else if (*psrc >= 'A' && *psrc <= 'Z') {
      *pdst++ = *psrc++;
    } else if (*psrc >= '0' && *psrc <= '9') {
      *pdst++ = *psrc++;
    } else if (*psrc == '-' || *psrc == '_' || *psrc == '.' || *psrc == '!' ||
               *psrc == '~' || *psrc == '*' || *psrc == '\'' || *psrc == '(' ||
               *psrc == ')') {
      *pdst++ = *psrc++;
    } else {
      if (*psrc >= 0xd800 && *psrc <= 0xdbff) {
        if (*(psrc + 1) < 0xdc00 || *(psrc + 1) > 0xdfff) {
          return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_URI, 0,
                                                    L"URI malformed");
        }
      }
      if (*psrc >= 0xdc00 && *psrc <= 0xdfff) {
        return neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_URI, 0,
                                                  L"URI malformed");
      }
      neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
      if (*psrc >= 0xd800 && *psrc <= 0xdbff) {
        wchar_t s[] = {*psrc, *(psrc + 1), 0};
        psrc += 2;
        uint8_t *utf8 = (uint8_t *)neo_wstring_to_string(allocator, s);
        for (size_t idx = 0; utf8[idx] != 0; idx++) {
          char ss[8];
          sprintf(ss, "%%%2X", utf8[idx]);
          for (size_t i = 0; ss[i] != 0; i++) {
            *pdst++ = ss[i];
          }
        }
        neo_allocator_free(allocator, utf8);
      } else {
        wchar_t s[] = {*psrc, 0};
        psrc++;
        uint8_t *utf8 = (uint8_t *)neo_wstring_to_string(allocator, s);
        for (size_t idx = 0; utf8[idx] != 0; idx++) {
          char ss[8];
          sprintf(ss, "%%%2X", utf8[idx]);
          for (size_t i = 0; ss[i] != 0; i++) {
            *pdst++ = ss[i];
          }
        }
        neo_allocator_free(allocator, utf8);
      }
    }
  }
  *pdst = 0;
  return neo_js_context_create_string(ctx, result);
}