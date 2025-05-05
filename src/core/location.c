#include "core/location.h"
#include "core/unicode.h"

noix_utf8_char noix_location_next(noix_position_t *location) {
  noix_utf8_char chr = noix_utf8_read_char(location->offset);
  if (*location->offset == '\r' && *(location->offset + 1) == '\n') {
    location->offset++;
  }
  if (*location->offset == 0xa || *location->offset == 0xd ||
      noix_utf8_char_is(chr, "\u2028") || noix_utf8_char_is(chr, "\u2029")) {
    location->line++;
    location->column = 1;
  } else {
    location->column += chr.end - chr.begin;
  }
  location->offset = chr.end;
  return chr;
}