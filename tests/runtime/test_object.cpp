#include "neo.js/core/allocator.h"
#include "neo.js/engine/context.h"
#include "neo.js/engine/runtime.h"
#include "neo.js/engine/variable.h"
#include "neo.js/runtime/constant.h"
#include <gtest/gtest.h>


class test_object : public testing::Test {};

TEST_F(test_object, constant_check) {
  neo_allocator_t allocator = neo_create_allocator(NULL);
  neo_js_runtime_t runtime = neo_create_js_runtime(allocator);
  neo_js_context_t ctx = neo_create_js_context(runtime);
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  {
    neo_js_variable_t constructor = neo_js_variable_get_field(
        constant->object_prototype, ctx, constant->key_constructor);
    ASSERT_EQ(constant->object_class->value, constructor->value);
  }
  {
    neo_js_variable_t prototype = neo_js_variable_get_field(
        constant->object_class, ctx, constant->key_prototype);
    ASSERT_EQ(constant->object_prototype->value, prototype->value);
  }
  {
    neo_js_variable_t prototype =
        neo_js_variable_get_prototype_of(constant->object_class, ctx);
    ASSERT_EQ(constant->function_prototype->value, prototype->value);
  }
  neo_allocator_free(allocator, ctx);
  neo_allocator_free(allocator, runtime);
  neo_delete_allocator(allocator);
}