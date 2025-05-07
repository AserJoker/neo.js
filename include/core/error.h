#ifndef _H_NEO_CORE_ERROR_
#define _H_NEO_CORE_ERROR_
#ifdef __cplusplus
extern "C" {
#endif
#include "core/allocator.h"
#include <stdbool.h>
typedef struct _neo_error_t *neo_error_t;

void neo_error_initialize(neo_allocator_t allocator);

bool neo_has_error();

void neo_push_error(const char *type, const char *message);

void neo_push_stack(const char *funcname, const char *filename, int32_t line);

neo_error_t neo_poll_error(const char *funcname, const char *filename,
                           int32_t line);

const char *neo_error_get_type(neo_error_t self);

const char *neo_error_get_message(neo_error_t self);

char *neo_error_to_string(neo_error_t self);
#define THROW(type, fmt, ...)                                                  \
  do {                                                                         \
    char message[4096];                                                        \
    sprintf(message, fmt, ##__VA_ARGS__);                                      \
    neo_push_error(type, message);                                             \
    neo_push_stack(__FUNCTION__, __FILE__, __LINE__);                          \
  } while (0)

#define CHECK_AND_THROW(cleanup)                                               \
  if (neo_has_error()) {                                                       \
    neo_push_stack(__FUNCTION__, __FILE__, __LINE__ - 1);                      \
    cleanup                                                                    \
  }

#define TRY(expr)                                                              \
  expr;                                                                        \
  if (neo_has_error())                                                         \
    neo_push_stack(__FUNCTION__, __FILE__, __LINE__);                          \
  if (neo_has_error())
#ifdef __cplusplus
};
#endif
#endif