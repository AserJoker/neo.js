#include "core/unicode.h"
#include <cstdio>
#include <gtest/gtest.h>

class TEST_unicode : public testing::Test {};

TEST_F(TEST_unicode, utf8_to_utf32) {
  const char *str = "\u2e28";
  noix_utf8_char chr = noix_utf8_read_char(str);
  uint32_t utf32 = noix_utf8_char_to_utf32(chr);
  ASSERT_EQ(utf32, 0x2e28);
}