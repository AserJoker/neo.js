#include "core/location.h"
#include "core/unicode.h"

neo_utf8_char neo_location_next(neo_position_t *location) {
  neo_utf8_char chr = neo_utf8_read_char(location->offset);
  if (*location->offset == '\r' && *(location->offset + 1) == '\n') {
    location->offset++;
  }
  if (*location->offset == 0xa || *location->offset == 0xd ||
      neo_utf8_char_is(chr, "\u2028") || neo_utf8_char_is(chr, "\u2029")) {
    location->line++;
    location->column = 1;
  } else {
    location->column += chr.end - chr.begin;
  }
  location->offset = chr.end;
  return chr;
}