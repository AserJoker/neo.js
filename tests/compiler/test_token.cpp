#include "compiler/token.h"
#include "core/allocator.h"
#include "core/location.h"
#include "core/position.h"
#include <gtest/gtest.h>
class neo_test_token : public testing::Test {
protected:
  neo_allocator_t allocator = NULL;

public:
  void SetUp() override { allocator = neo_create_allocator(NULL); }
  void TearDown() override {
    neo_delete_allocator(allocator);
    allocator = NULL;
  }
};

static neo_location_t create_location(const char *src) {
  neo_location_t loc = {};
  loc.begin = {1, 1, src};
  loc.end = loc.begin;
  return loc;
}

TEST_F(neo_test_token, identifier) {
  neo_position_t pos = {
      .line = 1,
      .column = 1,
      .offset = "",
  };
  neo_token_t token = neo_read_identify_token(allocator, "", &pos);
  ASSERT_EQ(token, (neo_token_t) nullptr);
  pos.offset = "123";
  token = neo_read_identify_token(allocator, "", &pos);
  ASSERT_EQ(token, (neo_token_t) nullptr);
  pos.offset = "+123";
  token = neo_read_identify_token(allocator, "", &pos);
  ASSERT_EQ(token, (neo_token_t) nullptr);
  pos.offset = "//aaa";
  token = neo_read_identify_token(allocator, "", &pos);
  ASSERT_EQ(token, (neo_token_t) nullptr);
  pos.offset = "/*aaa*/";
  token = neo_read_identify_token(allocator, "", &pos);
  ASSERT_EQ(token, (neo_token_t) nullptr);
  pos.offset = "/aaaa/g";
  token = neo_read_identify_token(allocator, "", &pos);
  ASSERT_EQ(token, (neo_token_t) nullptr);
  pos.offset = "\"aaaa\"";
  token = neo_read_identify_token(allocator, "", &pos);
  ASSERT_EQ(token, (neo_token_t) nullptr);
  neo_location_t loc;
  loc = create_location("abc");
  token = neo_read_identify_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_IDENTIFY);
  char *str = neo_location_get(allocator, loc);
  neo_allocator_free(allocator, token);
  ASSERT_EQ(std::string(str), "abc");
  neo_allocator_free(allocator, str);

  loc = create_location("abc   ");
  token = neo_read_identify_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_IDENTIFY);
  str = neo_location_get(allocator, loc);
  neo_allocator_free(allocator, token);
  ASSERT_EQ(std::string(str), "abc");
  neo_allocator_free(allocator, str);

  loc = create_location("#abc");
  token = neo_read_identify_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_IDENTIFY);
  str = neo_location_get(allocator, loc);
  neo_allocator_free(allocator, token);
  ASSERT_EQ(std::string(str), "#abc");
  neo_allocator_free(allocator, str);

  loc = create_location("\\u{0061}");
  token = neo_read_identify_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_IDENTIFY);
  str = neo_location_get(allocator, loc);
  neo_allocator_free(allocator, token);
  ASSERT_EQ(std::string(str), "a");
  neo_allocator_free(allocator, str);

  loc = create_location("abc#d");
  token = neo_read_identify_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_ERROR);
  neo_allocator_free(allocator, token);
}

TEST_F(neo_test_token, number) {
  neo_location_t loc = {};
  loc = create_location("");
  loc.end = loc.begin;
  neo_token_t token = nullptr;
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_EQ(token, nullptr);

  loc = create_location("abc");
  loc.end = loc.begin;
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_EQ(token, nullptr);

  loc = create_location("+");
  loc.end = loc.begin;
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_EQ(token, nullptr);

  loc = create_location("+123");
  loc.end = loc.begin;
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_EQ(token, nullptr);
  char *str = NULL;

  loc = create_location("0xa1f");
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  str = neo_location_get(allocator, token->location);
  ASSERT_EQ(std::string(str), "0xa1f");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("0Xa1f");
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  str = neo_location_get(allocator, token->location);
  ASSERT_EQ(std::string(str), "0Xa1f");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("0XA1f");
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  str = neo_location_get(allocator, token->location);
  ASSERT_EQ(std::string(str), "0XA1f");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("0xa1g");
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_ERROR);
  neo_allocator_free(allocator, token);

  loc = create_location("0o17");
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  str = neo_location_get(allocator, token->location);
  ASSERT_EQ(std::string(str), "0o17");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("0O17");
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  str = neo_location_get(allocator, token->location);
  ASSERT_EQ(std::string(str), "0O17");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("0o18");
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_ERROR);
  neo_allocator_free(allocator, token);

  loc = create_location("123");
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  str = neo_location_get(allocator, token->location);
  ASSERT_EQ(std::string(str), "123");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("123   ");
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  str = neo_location_get(allocator, token->location);
  ASSERT_EQ(std::string(str), "123");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("123_456");
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  str = neo_location_get(allocator, token->location);
  ASSERT_EQ(std::string(str), "123_456");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("123a");
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_ERROR);
  neo_allocator_free(allocator, token);

  loc = create_location("123.");
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  str = neo_location_get(allocator, token->location);
  ASSERT_EQ(std::string(str), "123.");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location(".123");
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  str = neo_location_get(allocator, token->location);
  ASSERT_EQ(std::string(str), ".123");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("1.e2");
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  str = neo_location_get(allocator, token->location);
  ASSERT_EQ(std::string(str), "1.e2");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("1.e+2");
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  str = neo_location_get(allocator, token->location);
  ASSERT_EQ(std::string(str), "1.e+2");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("1.e-2");
  token = neo_read_number_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  str = neo_location_get(allocator, token->location);
  ASSERT_EQ(std::string(str), "1.e-2");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);
}

TEST_F(neo_test_token, symbol) {
  neo_location_t loc = {};
  neo_token_t token = NULL;
  char *str;
  const char *symbols[] = {
      ">>>=",   "...", "<<=", ">>>", "===", "!==", "**=", ">>=", "&&=", "||=",
      "(?\?=)", "**",  "==",  "!=",  "<<",  ">>",  "<=",  ">=",  "&&",  "||",
      "??",     "++",  "--",  "+=",  "-=",  "*=",  "/=",  "%=",  "&=",  "^=",
      "|=",     "=>",  "?.",  "=",   "*",   "/",   "%",   "+",   "-",   "<",
      ">",      "&",   "^",   "|",   ",",   "!",   "~",   "(",   ")",   "[",
      "]",      "{",   "}",   "@",   "#",   ".",   "?",   ":",   ";",   0,
  };
  for (size_t idx = 0; symbols[idx] != 0; idx++) {
    loc = create_location(symbols[idx]);
    neo_token_t token = neo_read_symbol_token(allocator, "", &loc.end);
    ASSERT_NE(token, nullptr);
    ASSERT_EQ(token->type, NEO_TOKEN_TYPE_SYMBOL);
    str = neo_location_get(allocator, loc);
    ASSERT_EQ(std::string(str), symbols[idx]);
    neo_allocator_free(allocator, str);
    neo_allocator_free(allocator, token);
  }
}

TEST_F(neo_test_token, regexp) {
  neo_location_t loc = {};
  neo_token_t token = NULL;
  char *str = NULL;

  loc = create_location("/aaa/   ");
  token = neo_read_regexp_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_REGEXP);
  str = neo_location_get(allocator, loc);
  ASSERT_EQ(std::string(str), "/aaa/");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("/aa\\/a/   ");
  token = neo_read_regexp_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_REGEXP);
  str = neo_location_get(allocator, loc);
  ASSERT_EQ(std::string(str), "/aa\\/a/");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("/aaa/g   ");
  token = neo_read_regexp_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_REGEXP);
  str = neo_location_get(allocator, loc);
  ASSERT_EQ(std::string(str), "/aaa/g");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("/aaa/gmi   ");
  token = neo_read_regexp_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_REGEXP);
  str = neo_location_get(allocator, loc);
  ASSERT_EQ(std::string(str), "/aaa/gmi");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("/aaa   ");
  token = neo_read_regexp_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_ERROR);
  neo_allocator_free(allocator, token);
}

TEST_F(neo_test_token, string) {
  neo_location_t loc = {};
  neo_token_t token = NULL;
  char *str = NULL;

  loc = create_location("\"test\"    ");
  token = neo_read_string_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_STRING);
  str = neo_location_get(allocator, token->location);
  ASSERT_EQ(std::string(str), "\"test\"");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("\"test    ");
  token = neo_read_string_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_ERROR);
  neo_allocator_free(allocator, token);

  loc = create_location("'test'");
  token = neo_read_string_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_STRING);
  str = neo_location_get(allocator, token->location);
  ASSERT_EQ(std::string(str), "'test'");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("'\\x61'");
  token = neo_read_string_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_STRING);
  str = neo_location_get_raw(allocator, token->location);
  ASSERT_EQ(std::string(str), "'\\x64'");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("'\\u{0061}'");
  token = neo_read_string_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_STRING);
  str = neo_location_get_raw(allocator, token->location);
  ASSERT_EQ(std::string(str), "'\\u{0064}'");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);

  loc = create_location("'\\n'");
  token = neo_read_string_token(allocator, "", &loc.end);
  ASSERT_NE(token, nullptr);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_STRING);
  str = neo_location_get_raw(allocator, token->location);
  ASSERT_EQ(std::string(str), "'\\n'");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, token);
}