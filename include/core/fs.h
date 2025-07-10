#ifndef _H_NEO_CORE_FS_
#define _H_NEO_CORE_FS_
#include "core/allocator.h"
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif

char *neo_fs_read_file(neo_allocator_t allocator, const wchar_t *filename);

#ifdef __cplusplus
};
#endif
#endif