#ifndef _H_NEO_COMPILER_TOKEN_
#define _H_NEO_COMPILER_TOKEN_
#include "core/allocator.h"
#include "core/location.h"
#include "core/position.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum _neo_token_type_t {
  NEO_TOKEN_TYPE_STRING,
  NEO_TOKEN_TYPE_NUMBER,
  NEO_TOKEN_TYPE_SYMBOL,
  NEO_TOKEN_TYPE_REGEXP,
  NEO_TOKEN_TYPE_IDENTIFY,
  NEO_TOKEN_TYPE_PRIVATE_IDENTIFY,
  NEO_TOKEN_TYPE_COMMENT,
  NEO_TOKEN_TYPE_MULTILINE_COMMENT,
  NEO_TOKEN_TYPE_TEMPLATE_STRING,
  NEO_TOKEN_TYPE_TEMPLATE_STRING_START,
  NEO_TOKEN_TYPE_TEMPLATE_STRING_END,
  NEO_TOKEN_TYPE_TEMPLATE_STRING_PART,
} neo_token_type_t;

typedef struct _neo_token_t {
  neo_token_type_t type;
  neo_location_t location;
} *neo_token_t;

neo_token_t neo_read_string_token(neo_allocator_t allocator, const char *file,
                                  neo_position_t *positon);

neo_token_t neo_read_number_token(neo_allocator_t allocator, const char *file,
                                  neo_position_t *position);

neo_token_t neo_read_symbol_token(neo_allocator_t allocator, const char *file,
                                  neo_position_t *position);

neo_token_t neo_read_regexp_token(neo_allocator_t allocator, const char *file,
                                  neo_position_t *position);

neo_token_t neo_read_identify_token(neo_allocator_t allocator, const char *file,
                                    neo_position_t *position);

neo_token_t neo_read_comment_token(neo_allocator_t allocator, const char *file,
                                   neo_position_t *position);

neo_token_t neo_read_multiline_comment_token(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position);

neo_token_t neo_read_template_string_token(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif