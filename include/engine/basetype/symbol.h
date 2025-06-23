#ifndef _H_NEO_ENGINE_BASETYPE_SYMBOL_
#define _H_NEO_ENGINE_BASETYPE_SYMBOL_
#include "core/allocator.h"
#include "engine/type.h"
#include "engine/value.h"
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_engine_symbol_t *neo_engine_symbol_t;

struct _neo_engine_symbol_t {
  struct _neo_engine_value_t value;
  wchar_t *description;
};

neo_engine_type_t neo_get_js_symbol_type();

neo_engine_symbol_t neo_create_js_symbol(neo_allocator_t allocator,
                                         const wchar_t *value);

neo_engine_symbol_t neo_engine_value_to_symbol(neo_engine_value_t value);

#ifdef __cplusplus
}
#endif
#endif