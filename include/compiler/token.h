#ifndef _H_NOIX_COMPILER_TOKEN_
#define _H_NOIX_COMPILER_TOKEN_
#include "core/allocator.h"
#include "location.h"
typedef enum _noix_token_type {
  NOIX_TOKEN_STRING,
  NOIX_TOKEN_TEMPLATE_STRING,
  NOIX_TOKEN_TEMPLATE_STRING_START,
  NOIX_TOKEN_TEMPLATE_STRING_END,
  NOIX_TOKEN_TEMPLATE_STRING_PART,
  NOIX_TOKEN_NUMBER,
  NOIX_TOKEN_SYMBOL,
  NOIX_TOKEN_IDENTITY,
  NOIX_TOKEN_REGEXP,
  NOIX_TOKEN_WHITE_SPACE,
  NOIX_TOKEN_LINE_TERMINATOR,
  NOIX_TOKEN_COMMENT,
  NOIX_TOKEN_MULTILINE_COMMENT,
  NOIX_TOKEN_PRIVATE_NAME,
} noix_token_type;

typedef struct _noix_token_t {
  noix_location_t location;
  noix_token_type type;
} *noix_token_t;

noix_token_t noix_read_string_token(noix_allocator_t allocator,
                                    noix_position_t *current);

noix_token_t noix_read_number_token(noix_allocator_t allocator,
                                    noix_position_t *current);

noix_token_t noix_read_symbol_token(noix_allocator_t allocator,
                                    noix_position_t *current);

noix_token_t noix_read_identify_token(noix_allocator_t allocator,
                                      noix_position_t *current);

noix_token_t noix_read_regexp_token(noix_allocator_t allocator,
                                    noix_position_t *current);

noix_token_t noix_read_white_space_token(noix_allocator_t allocator,
                                         noix_position_t *current);

noix_token_t noix_read_line_terminator_token(noix_allocator_t allocator,
                                             noix_position_t *current);

noix_token_t noix_read_template_string(noix_allocator_t allocator,
                                       noix_position_t *current);

noix_token_t noix_read_template_string_start_token(noix_allocator_t allocator,
                                                   noix_position_t *current);

noix_token_t noix_read_template_string_end_token(noix_allocator_t allocator,
                                                 noix_position_t *current);

noix_token_t noix_read_template_string_part_token(noix_allocator_t allocator,
                                                  noix_position_t *current);

#endif