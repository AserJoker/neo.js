#ifndef _H_NEO_ENGINE_RUNTIME_
#define _H_NEO_ENGINE_RUNTIME_
#include "core/allocator.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_engine_runtime_t *neo_engine_runtime_t;

neo_engine_runtime_t neo_create_js_runtime(neo_allocator_t allocator);

neo_allocator_t neo_engine_runtime_get_allocator(neo_engine_runtime_t self);

#ifdef __cplusplus
}
#endif
#endif