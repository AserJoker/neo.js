#ifndef _H_NEO_CORE_PATH_
#define _H_NEO_CORE_PATH_

#include "neo.js/core/allocator.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_path_t *neo_path_t;

neo_path_t neo_create_path(neo_allocator_t allocator, const char *source);

neo_path_t neo_path_concat(neo_path_t current, neo_path_t another);

bool neo_path_append(neo_path_t path, const char *part);

neo_path_t neo_path_clone(neo_path_t current);

neo_path_t neo_path_current(neo_allocator_t allocator);

neo_path_t neo_path_absolute(neo_path_t current);

neo_path_t neo_path_parent(neo_path_t current);

const char *neo_path_filename(neo_path_t current);

const char *neo_path_extname(neo_path_t current);

char *neo_path_to_string(neo_path_t path);

#ifdef __cplusplus
};
#endif
#endif