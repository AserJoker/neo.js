#ifndef _H_NEO_CORE_JSON_
#define _H_NEO_CORE_JSON_
#include "core/allocator.h"
#include "variable.h"
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif
wchar_t *neo_json_stringify(neo_allocator_t allocator, neo_variable_t variable);
neo_variable_t neo_json_parse(neo_allocator_t allocator, const wchar_t *file,
                              const char *source);
#ifdef __cplusplus
};
#endif
#endif