#ifndef _H_NEO_CORE_JSON_
#define _H_NEO_CORE_JSON_
#include "core/allocator.h"
#include "core/string.h"
#include "variable.h"

neo_string_t neo_json_stringify(neo_allocator_t allocator,
                                neo_variable_t variable);
#endif