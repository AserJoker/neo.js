#include "core/allocator.h"
#include <stdint.h>
#ifndef _H_NEO_ENGINE_STACKFRAME_
#ifdef __cplusplus
extern "C" {
#endif
struct _neo_js_stackframe_t {
  const uint16_t *filename;
  const uint16_t *funcname;
  uint32_t line;
  uint32_t column;
};
typedef struct _neo_js_stackframe_t *neo_js_stackframe_t;
neo_js_stackframe_t neo_create_js_stackframe(neo_allocator_t allocator,
                                             const uint16_t *filename,
                                             const uint16_t *funcname,
                                             uint32_t line, uint32_t column);
uint16_t *neo_js_stackframe_to_string(neo_allocator_t allocator,
                                      neo_js_stackframe_t frame);

#ifdef __cplusplus
}
#endif
#endif