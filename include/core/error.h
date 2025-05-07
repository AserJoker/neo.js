#ifndef _H_NOIX_CORE_ERROR_
#define _H_NOIX_CORE_ERROR_
#ifdef __cplusplus
extern "C" {
#endif
#include "core/allocator.h"
#include <stdbool.h>
typedef struct _noix_error_t *noix_error_t;

void noix_error_initialize(noix_allocator_t allocator);

bool noix_has_error();

void noix_push_error(const char *type, const char *message);

void noix_push_stack(const char *funcname, const char *filename, int32_t line);

noix_error_t noix_poll_error(const char *funcname, const char *filename,
                             int32_t line);

const char *noix_error_get_type(noix_error_t self);

const char *noix_error_get_message(noix_error_t self);

char *noix_error_to_string(noix_error_t self);
#define THROW(type, fmt, ...)                                                  \
  do {                                                                         \
    char message[4096];                                                        \
    sprintf(message, fmt, ##__VA_ARGS__);                                      \
    noix_push_error(type, message);                                            \
    noix_push_stack(__FUNCTION__, __FILE__, __LINE__);                         \
  } while (0)

#define CHECK_AND_THROW(cleanup)                                               \
  if (noix_has_error()) {                                                      \
    noix_push_stack(__FUNCTION__, __FILE__, __LINE__ - 1);                     \
    cleanup                                                                    \
  }

#define TRY(expr)                                                              \
  expr;                                                                        \
  if (noix_has_error())                                                        \
    noix_push_stack(__FUNCTION__, __FILE__, __LINE__);                         \
  if (noix_has_error())
#ifdef __cplusplus
};
#endif
#endif