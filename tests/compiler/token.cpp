#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/position.h"
#include <gtest/gtest.h>
#include <iostream>
#include <string>

class TEST_token : public testing::Test {
protected:
  neo_allocator_t _allocator;

public:
  void SetUp() override {
    _allocator = neo_create_default_allocator();
    neo_error_initialize(_allocator);
  }

  void TearDown() override {
    if (neo_has_error()) {
      neo_error_t error = neo_poll_error(__FUNCTION__, __FILE__, __LINE__);
      if (error) {
        char *msg = neo_error_to_string(error);
        std::cerr << msg << std::endl;
        neo_allocator_free(_allocator, msg);
        neo_allocator_free(_allocator, error);
      }
    }
    neo_delete_allocator(_allocator);
    _allocator = NULL;
  }
};

TEST_F(TEST_token, read_string_token_base) {
  const char *str = "'hello world'";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  neo_token_t token = neo_read_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_STRING);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      str);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_string_token_re) {
  const char *str = "'hello\\' world'";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  neo_token_t token = neo_read_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_STRING);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      str);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_string_token_end) {
  const char *str = "'hello world'.length";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  neo_token_t token = neo_read_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_STRING);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "'hello world'");
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_string_token_line_terminal_end) {
  const char *str = "'hello world\n'";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  neo_token_t token = neo_read_string_token(_allocator, "test.js", &position);
  ASSERT_TRUE(neo_has_error());
  ASSERT_TRUE(token == NULL);
  neo_error_t error = neo_poll_error(__FUNCTION__, __FILE__, __LINE__);
  char *msg = neo_error_to_string(error);
  ASSERT_TRUE(
      std::string(msg).starts_with("SyntaxError: Invalid or unexpected token"));
  neo_allocator_free(_allocator, msg);
  neo_allocator_free(_allocator, error);
}

TEST_F(TEST_token, read_string_token_unicode_line_terminal_end) {
  const char *str = "'hello world\u2028'";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  neo_token_t token = neo_read_string_token(_allocator, "test.js", &position);
  ASSERT_TRUE(neo_has_error());
  ASSERT_TRUE(token == NULL);
  neo_error_t error = neo_poll_error(__FUNCTION__, __FILE__, __LINE__);
  char *msg = neo_error_to_string(error);
  ASSERT_TRUE(
      std::string(msg).starts_with("SyntaxError: Invalid or unexpected token"));
  neo_allocator_free(_allocator, msg);
  neo_allocator_free(_allocator, error);
}

TEST_F(TEST_token, read_string_token_lost_end) {
  const char *str = "'hello world";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  neo_token_t token = neo_read_string_token(_allocator, "test.js", &position);
  ASSERT_TRUE(neo_has_error());
  ASSERT_TRUE(token == NULL);
  neo_error_t error = neo_poll_error(__FUNCTION__, __FILE__, __LINE__);
  char *msg = neo_error_to_string(error);
  ASSERT_TRUE(
      std::string(msg).starts_with("SyntaxError: Invalid or unexpected token"));
  neo_allocator_free(_allocator, msg);
  neo_allocator_free(_allocator, error);
}

TEST_F(TEST_token, read_string_token_with_hex) {
  const char *str = "'hello world\\x11'";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  neo_token_t token = neo_read_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_STRING);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      str);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_string_token_with_unicode) {
  const char *str = "'hello world\\u4f40'";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  neo_token_t token = neo_read_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_STRING);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      str);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_string_token_with_unicode_ex) {
  const char *str = "'hello world\\u{20000}'";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  neo_token_t token = neo_read_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_STRING);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      str);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_base) {
  const char *source = "123";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_oct) {
  const char *source = "0o100";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_oct2) {
  const char *source = "0O100";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_oct_invalid) {
  const char *source = "0o8";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_number_token(_allocator, "test.js", &position);
  ASSERT_TRUE(neo_has_error());
  ASSERT_TRUE(token == NULL);
  neo_error_t error = neo_poll_error(__FUNCTION__, __FILE__, __LINE__);
  ASSERT_EQ(neo_error_get_type(error), std::string("SyntaxError"));
  ASSERT_TRUE(std::string(neo_error_get_message(error))
                  .starts_with("Invalid or unexpected token"));
  neo_allocator_free(_allocator, error);
}

TEST_F(TEST_token, read_number_token_hex_invalid) {
  const char *source = "0x";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_number_token(_allocator, "test.js", &position);
  ASSERT_TRUE(neo_has_error());
  ASSERT_TRUE(token == NULL);
  neo_error_t error = neo_poll_error(__FUNCTION__, __FILE__, __LINE__);
  ASSERT_EQ(neo_error_get_type(error), std::string("SyntaxError"));
  ASSERT_TRUE(std::string(neo_error_get_message(error))
                  .starts_with("Invalid or unexpected token"));
  neo_allocator_free(_allocator, error);
}

TEST_F(TEST_token, read_number_token_float) {
  const char *source = "123.456";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_float2) {
  const char *source = ".456";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_float3) {
  const char *source = "1.";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_exp) {
  const char *source = "1e2";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_exp2) {
  const char *source = "1e+2";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_number_token_exp3) {
  const char *source = ".2e+2";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_number_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_NUMBER);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_symbol_token_base) {
  const char *source = "+1";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_symbol_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_SYMBOL);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "+");
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_symbol_token_greedy) {
  const char *source = "++a";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_symbol_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_SYMBOL);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "++");
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_regexp_token_base) {
  const char *source = "/test/";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_regexp_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_REGEXP);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_regexp_token_re) {
  const char *source = "/te\\/st/";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_regexp_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_REGEXP);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_regexp_token_re2) {
  const char *source = "/te[/]st/";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_regexp_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_REGEXP);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      source);
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_identify_base) {
  const char *source = "$test data";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_identify_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_IDENTIFY);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "$test");
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_identify_unicode) {
  const char *source = "t\\u{aa}est data";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_identify_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_IDENTIFY);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "t\\u{aa}est");
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_identify_unicode2) {
  const char *source = "t\\u00aaest data";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_identify_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_IDENTIFY);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "t\\u00aaest");
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_identify_unicode3) {
  const char *source = "tªest data";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_identify_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_IDENTIFY);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "tªest");
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_comment_base) {
  const char *source = "//comment";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_comment_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_COMMENT);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "//comment");
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_comment_base2) {
  const char *source = "//comment\nabc";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token = neo_read_comment_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_COMMENT);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "//comment");
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_multiline_comment_base) {
  const char *source = "/*aaaaaaaaaa*/";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token =
      neo_read_multiline_comment_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_MULTILINE_COMMENT);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "/*aaaaaaaaaa*/");
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_multiline_comment_base2) {
  const char *source = "/*abc\ndef*/";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token =
      neo_read_multiline_comment_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_MULTILINE_COMMENT);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "/*abc\ndef*/");
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_template_string_base) {
  const char *source = "`ab\nc`";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token =
      neo_read_template_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_TEMPLATE_STRING);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "`ab\nc`");
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_template_string_base2) {
  const char *source = "`ab\n\\tc` def";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token =
      neo_read_template_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_TEMPLATE_STRING);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "`ab\n\\tc`");
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_template_string_start) {
  const char *source = "`abc${def}`";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token =
      neo_read_template_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_TEMPLATE_STRING_START);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "`abc${");
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_template_string_start2) {
  const char *source = "`abc\\${def}`";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token =
      neo_read_template_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_TEMPLATE_STRING);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "`abc\\${def}`");
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_template_string_part) {
  const char *source = "}test\\${${def";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token =
      neo_read_template_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_TEMPLATE_STRING_PART);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "}test\\${${");
  neo_allocator_free(_allocator, token);
}

TEST_F(TEST_token, read_template_string_end) {
  const char *source = "}\\`aaa`def";
  neo_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = source;
  neo_token_t token =
      neo_read_template_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NEO_TOKEN_TYPE_TEMPLATE_STRING_END);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      "}\\`aaa`");
  neo_allocator_free(_allocator, token);
}