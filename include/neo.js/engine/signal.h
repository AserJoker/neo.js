#ifndef _H_NEO_ENGINE_SIGNAL_
#define _H_NEO_ENGINE_SIGNAL_
#include "core/allocator.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif

#define NEO_JS_SIGNAL_BREAK 0
#define NEO_JS_SIGNAL_CONTINUE 1
#define NEO_JS_SIGNAL_CUSTOM 2
typedef struct _neo_js_signal_t *neo_js_signal_t;
struct _neo_js_signal_t {
  struct _neo_js_value_t super;
  uint32_t type;
  const void *msg;
};
neo_js_signal_t neo_create_js_signal(neo_allocator_t allocator, uint32_t type,
                                    const void *msg);
void neo_init_js_signal(neo_js_signal_t self, neo_allocator_t allocaotr,
                        uint32_t type,const void *msg);
void neo_deinit_js_signal(neo_js_signal_t self, neo_allocator_t allocaotr);
neo_js_value_t neo_js_signal_to_value(neo_js_signal_t self);
#ifdef __cplusplus
}
#endif
#endif