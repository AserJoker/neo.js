#ifndef _H_NEO_ENGINE_RUNTIME_
#define _H_NEO_ENGINE_RUNTIME_
#include "compiler/program.h"
#include "core/allocator.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_runtime_t *neo_js_runtime_t;

neo_js_runtime_t neo_create_js_runtime(neo_allocator_t allocator);

neo_allocator_t neo_js_runtime_get_allocator(neo_js_runtime_t self);

neo_program_t neo_js_runtime_get_program(neo_js_runtime_t self,
                                         const char *filename);

void neo_js_runtime_set_program(neo_js_runtime_t self, const char *filename,
                                neo_program_t program);

#ifdef __cplusplus
}
#endif
#endif