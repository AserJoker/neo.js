#ifndef _H_CORE_STRING_
#define _H_CORE_STRING_
#include "core/allocator.h"
char *neo_string_concat(neo_allocator_t allocator, char *src, size_t *max,
                        const char *str);
#endif