#ifndef _H_NEO_JS_STACKFRAME_
#define _H_NEO_JS_STACKFRAME_
#include "core/allocator.h"
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_stackframe_t {
  const wchar_t *filename;
  wchar_t *function;
  uint32_t line;
  uint32_t column;
} *neo_js_stackframe_t;
neo_js_stackframe_t neo_create_js_stackframe(neo_allocator_t allocator);
#ifdef __cplusplus
}
#endif
#endif