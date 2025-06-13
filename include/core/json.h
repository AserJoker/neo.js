#ifndef _H_NEO_CORE_JSON_
#define _H_NEO_CORE_JSON_
#include "core/allocator.h"
#include "variable.h"
#ifdef __cplusplus
extern "C" {
#endif
char *neo_json_stringify(neo_allocator_t allocator, neo_variable_t variable);
#ifdef __cplusplus
};
#endif
#endif