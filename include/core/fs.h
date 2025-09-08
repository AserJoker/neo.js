#ifndef _H_NEO_CORE_FS_
#define _H_NEO_CORE_FS_
#include "core/allocator.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

char *neo_fs_read_file(neo_allocator_t allocator, const char *filename);

bool neo_fs_is_dir(neo_allocator_t allocator, const char *filename);

bool neo_fs_exist(neo_allocator_t allocator, const char *filename);

#ifdef __cplusplus
};
#endif
#endif