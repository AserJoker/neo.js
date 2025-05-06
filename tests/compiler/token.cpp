#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/position.h"
#include <gtest/gtest.h>
#include <iostream>
#include <string>

class TEST_token : public testing::Test {
protected:
  noix_allocator_t _allocator;

public:
  void SetUp() override {
    _allocator = noix_create_default_allocator();
    noix_error_initialize(_allocator);
  }

  void TearDown() override {
    if (noix_has_error()) {
      noix_error_t error = noix_poll_error(__FUNCTION__, __FILE__, __LINE__);
      if (error) {
        char *msg = noix_error_to_string(error);
        std::cerr << msg << std::endl;
        noix_allocator_free(_allocator, msg);
        noix_allocator_free(_allocator, error);
      }
    }
    noix_delete_allocator(_allocator);
    _allocator = NULL;
  }
};

TEST_F(TEST_token, read_string_token_base) {
  const char *str = "'hello world'";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  noix_token_t token = noix_read_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_STRING);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      str);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_string_token_re) {
  const char *str = "'hello\\' world'";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  noix_token_t token = noix_read_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_STRING);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      str);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_string_token_end) {
  const char *str = "'hello world'.length";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  noix_token_t token = noix_read_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_STRING);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "'hello world'");
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_string_token_line_terminal_end) {
  const char *str = "'hello world\n'";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  noix_token_t token = noix_read_string_token(_allocator, "test.js", &position);
  ASSERT_TRUE(noix_has_error());
  ASSERT_TRUE(token == NULL);
  noix_error_t error = noix_poll_error(__FUNCTION__, __FILE__, __LINE__);
  char *msg = noix_error_to_string(error);
  ASSERT_TRUE(
      std::string(msg).starts_with("SyntaxError: Invalid or unexpected token"));
  noix_allocator_free(_allocator, msg);
  noix_allocator_free(_allocator, error);
}

TEST_F(TEST_token, read_string_token_unicode_line_terminal_end) {
  const char *str = "'hello world\u2028'";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  noix_token_t token = noix_read_string_token(_allocator, "test.js", &position);
  ASSERT_TRUE(noix_has_error());
  ASSERT_TRUE(token == NULL);
  noix_error_t error = noix_poll_error(__FUNCTION__, __FILE__, __LINE__);
  char *msg = noix_error_to_string(error);
  ASSERT_TRUE(
      std::string(msg).starts_with("SyntaxError: Invalid or unexpected token"));
  noix_allocator_free(_allocator, msg);
  noix_allocator_free(_allocator, error);
}

TEST_F(TEST_token, read_string_token_lost_end) {
  const char *str = "'hello world";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  noix_token_t token = noix_read_string_token(_allocator, "test.js", &position);
  ASSERT_TRUE(noix_has_error());
  ASSERT_TRUE(token == NULL);
  noix_error_t error = noix_poll_error(__FUNCTION__, __FILE__, __LINE__);
  char *msg = noix_error_to_string(error);
  ASSERT_TRUE(
      std::string(msg).starts_with("SyntaxError: Invalid or unexpected token"));
  noix_allocator_free(_allocator, msg);
  noix_allocator_free(_allocator, error);
}

TEST_F(TEST_token, read_string_token_with_hex) {
  const char *str = "'hello world\\x11'";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  noix_token_t token = noix_read_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_STRING);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      str);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_string_token_with_unicode) {
  const char *str = "'hello world\\u4f40'";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  noix_token_t token = noix_read_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_STRING);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      str);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_string_token_with_unicode_ex) {
  const char *str = "'hello world\\u{20000}'";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  noix_token_t token = noix_read_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_STRING);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      str);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_base) {
  const char *source = "123";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token = noix_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_oct) {
  const char *source = "0o100";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token = noix_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_oct2) {
  const char *source = "0O100";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token = noix_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_oct_invalid) {
  const char *source = "0o8";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token = noix_read_number_token(_allocator, "test.js", &position);
  ASSERT_TRUE(noix_has_error());
  ASSERT_TRUE(token == NULL);
  noix_error_t error = noix_poll_error(__FUNCTION__, __FILE__, __LINE__);
  ASSERT_EQ(noix_error_get_type(error), std::string("SyntaxError"));
  ASSERT_TRUE(std::string(noix_error_get_message(error))
                  .starts_with("Invalid or unexpected token"));
  noix_allocator_free(_allocator, error);
}

TEST_F(TEST_token, read_number_token_hex_invalid) {
  const char *source = "0x";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token = noix_read_number_token(_allocator, "test.js", &position);
  ASSERT_TRUE(noix_has_error());
  ASSERT_TRUE(token == NULL);
  noix_error_t error = noix_poll_error(__FUNCTION__, __FILE__, __LINE__);
  ASSERT_EQ(noix_error_get_type(error), std::string("SyntaxError"));
  ASSERT_TRUE(std::string(noix_error_get_message(error))
                  .starts_with("Invalid or unexpected token"));
  noix_allocator_free(_allocator, error);
}

TEST_F(TEST_token, read_number_token_float) {
  const char *source = "123.456";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token = noix_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_float2) {
  const char *source = ".456";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token = noix_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_float3) {
  const char *source = "1.";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token = noix_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_exp) {
  const char *source = "1e2";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token = noix_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_exp2) {
  const char *source = "1e+2";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token = noix_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_exp3) {
  const char *source = ".2e+2";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token = noix_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_symbol_token_base) {
  const char *source = "+1";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token = noix_read_symbol_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_SYMBOL);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "+");
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_symbol_token_greedy) {
  const char *source = "++a";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token = noix_read_symbol_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_SYMBOL);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "++");
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_regexp_token_base) {
  const char *source = "/test/";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token = noix_read_regexp_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_REGEXP);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_regexp_token_re) {
  const char *source = "/te\\/st/";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token = noix_read_regexp_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_REGEXP);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_regexp_token_re2) {
  const char *source = "/te[/]st/";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token = noix_read_regexp_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_REGEXP);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_identify_base) {
  const char *source = "$test data";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token =
      noix_read_identify_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_IDENTIFY);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "$test");
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_identify_unicode) {
  const char *source = "t\\u{aa}est data";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token =
      noix_read_identify_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_IDENTIFY);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "t\\u{aa}est");
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_identify_unicode2) {
  const char *source = "t\\u00aaest data";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token =
      noix_read_identify_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_IDENTIFY);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "t\\u00aaest");
  noix_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_identify_unicode3) {
  const char *source = "tªest data";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  noix_token_t token =
      noix_read_identify_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_IDENTIFY);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "tªest");
  noix_allocator_free(_allocator, token);
}