#ifndef _H_NEO_ENGINE_FUNCTION_
#define _H_NEO_ENGINE_FUNCTION_
#include "core/allocator.h"
#include "core/hash_map.h"
#include "engine/object.h"
#include "engine/variable.h"
struct _neo_js_function_t {
  struct _neo_js_object_t super;
  bool native;
  bool async;
  const char *filename;
  uint32_t line;
  uint32_t column;
  neo_hash_map_t closure;
};
typedef struct _neo_js_function_t *neo_js_function_t;
neo_js_function_t neo_create_js_function(neo_allocator_t allocator, bool native,
                                         bool async, const char *filename,
                                         uint32_t line, uint32_t column,
                                         neo_js_variable_t prototype);
void neo_init_js_function(neo_js_function_t self, neo_allocator_t allocaotr,
                          bool native, bool async, const char *filename,
                          uint32_t line, uint32_t column,
                          neo_js_variable_t prototype);
void neo_deinit_js_function(neo_js_function_t self, neo_allocator_t allocaotr);
neo_js_value_t neo_js_function_to_value(neo_js_function_t self);
#endif