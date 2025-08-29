#include "core/allocator.h"
#include "engine/context.h"
#include "engine/runtime.h"
#include "engine/type.h"
#include <gtest/gtest.h>
class neo_test_engine : public testing::Test {
protected:
  neo_allocator_t allocator;
  neo_js_runtime_t runtime;
  neo_js_context_t ctx;

public:
  void SetUp() override {
    allocator = neo_create_allocator(NULL);
    runtime = neo_create_js_runtime(allocator);
    ctx = neo_create_js_context(allocator, runtime);
  }
  void TearDown() override {
    neo_allocator_free(allocator, ctx);
    neo_allocator_free(allocator, runtime);
    neo_delete_allocator(allocator);
  }
};

TEST_F(neo_test_engine, gc) {
  neo_js_variable_t null = neo_js_context_create_null(ctx);
  neo_js_variable_t object = neo_js_context_create_object(ctx, null);
}
