#include "engine/function.h"
#include "core/allocator.h"
#include "core/common.h"
#include "core/map.h"
#include "core/string.h"
#include "engine/object.h"
#include <stdbool.h>
static void neo_js_function_dispose(neo_allocator_t allocator,
                                    neo_js_function_t self) {
  neo_deinit_js_function(self, allocator);
}
neo_js_function_t neo_create_js_function(neo_allocator_t allocator, bool native,
                                         bool async, const char *filename,
                                         uint32_t line, uint32_t column,
                                         neo_js_value_t prototype) {
  neo_js_function_t func = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_function_t), neo_js_function_dispose);
  neo_init_js_function(func, allocator, native, async, filename, line, column,
                       prototype);
  return func;
}
void neo_init_js_function(neo_js_function_t self, neo_allocator_t allocaotr,
                          bool native, bool async, const char *filename,
                          uint32_t line, uint32_t column,
                          neo_js_value_t prototype) {
  neo_init_js_object(&self->super, allocaotr, prototype);
  self->native = native;
  self->async = async;
  self->filename = filename;
  self->line = line;
  self->column = column;
  neo_map_initialize_t initialize = {};
  initialize.compare = (neo_compare_fn_t)neo_string16_compare;
  initialize.auto_free_key = true;
  initialize.auto_free_value = false;
  self->closure = neo_create_map(allocaotr, &initialize);
}
void neo_deinit_js_function(neo_js_function_t self, neo_allocator_t allocaotr) {
  neo_allocator_free(allocaotr, self->closure);
  neo_deinit_js_object(&self->super, allocaotr);
}
neo_js_value_t neo_js_function_to_value(neo_js_function_t self) {
  return neo_js_object_to_value(&self->super);
}