#ifndef _H_NEO_CORE_STRING_
#define _H_NEO_CORE_STRING_
#include "core/allocator.h"
typedef struct _neo_string_t *neo_string_t;

neo_string_t neo_create_string(neo_allocator_t allocator, const char *source);

neo_string_t neo_clone_string(neo_allocator_t allocator, neo_string_t source);

size_t neo_string_get_length(neo_string_t self);

const char *neo_string_get(neo_string_t self);

neo_string_t neo_string_concat(neo_string_t self, const char *another);

#endif