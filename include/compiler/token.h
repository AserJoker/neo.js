#ifndef _H_NOIX_COMPILER_TOKEN_
#define _H_NOIX_COMPILER_TOKEN_
#include "core/allocator.h"
#include "core/location.h"
#include "core/position.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum _noix_token_type_t {
  NOIX_TOKEN_TYPE_STRING,
  NOIX_TOKEN_TYPE_NUMBER,
  NOIX_TOKEN_TYPE_SYMBOL,
  NOIX_TOKEN_TYPE_REGEXP,
  NOIX_TOKEN_TYPE_IDENTIFY,
  NOIX_TOKEN_TYPE_COMMENT,
  NOIX_TOKEN_TYPE_MULTILINE_COMMENT,
  NOIX_TOKEN_TYPE_TEMPLATE_STRING,
  NOIX_TOKEN_TYPE_TEMPLATE_STRING_START,
  NOIX_TOKEN_TYPE_TEMPLATE_STRING_END,
  NOIX_TOKEN_TYPE_TEMPLATE_STRING_PART,
} noix_token_type_t;

typedef struct _noix_token_t {
  noix_token_type_t type;
  noix_location_t position;
} *noix_token_t;

noix_token_t noix_read_string_token(noix_allocator_t allocator,
                                    const char *file, noix_position_t *positon);

noix_token_t noix_read_number_token(noix_allocator_t allocator,
                                    const char *file,
                                    noix_position_t *position);

noix_token_t noix_read_symbol_token(noix_allocator_t allocator,
                                    const char *file,
                                    noix_position_t *position);

noix_token_t noix_read_regexp_token(noix_allocator_t allocator,
                                    const char *file,
                                    noix_position_t *position);

noix_token_t noix_read_identify_token(noix_allocator_t allocator,
                                      const char *file,
                                      noix_position_t *position);

noix_token_t noix_read_comment_token(noix_allocator_t allocator,
                                     const char *file,
                                     noix_position_t *position);

noix_token_t noix_read_multiline_comment_token(noix_allocator_t allocator,
                                               const char *file,
                                               noix_position_t *position);

noix_token_t noix_read_template_string_token(noix_allocator_t allocator,
                                             const char *file,
                                             noix_position_t *position);

noix_token_t noix_read_template_string_start_token(noix_allocator_t allocator,
                                                   const char *file,
                                                   noix_position_t *position);

noix_token_t noix_read_template_string_part_token(noix_allocator_t allocator,
                                                  const char *file,
                                                  noix_position_t *position);

noix_token_t noix_read_template_string_end_token(noix_allocator_t allocator,
                                                 const char *file,
                                                 noix_position_t *position);
#ifdef __cplusplus
}
#endif
#endif