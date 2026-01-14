#include "neo.js/engine/callable.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/common.h"
#include "neo.js/core/map.h"
#include "neo.js/engine/object.h"
#include "neo.js/engine/value.h"
#include <stdbool.h>
#include <string.h>

static void neo_js_callable_dispose(neo_allocator_t allocator,
                                    neo_js_callable_t self) {
  neo_deinit_js_callable(self, allocator);
}
void neo_init_js_callable(neo_js_callable_t self, neo_allocator_t allocaotr,
                          bool native, bool async, bool generator,

                          neo_js_value_t prototype) {
  neo_init_js_object(&self->super, allocaotr, prototype);
  self->is_native = native;
  self->is_async = async;
  self->is_generator = generator;
  self->is_lambda = false;
  self->is_class = false;
  self->bind = NULL;
  self->clazz = NULL;
  neo_map_initialize_t initialize = {};
  initialize.compare = (neo_compare_fn_t)strcmp;
  initialize.auto_free_key = true;
  initialize.auto_free_value = false;
  self->closure = neo_create_map(allocaotr, &initialize);
  neo_js_value_t value = neo_js_callable_to_value(self);
  value->type = NEO_JS_TYPE_FUNCTION;
}
void neo_deinit_js_callable(neo_js_callable_t self, neo_allocator_t allocaotr) {
  neo_allocator_free(allocaotr, self->closure);
  neo_deinit_js_object(&self->super, allocaotr);
}
neo_js_value_t neo_js_callable_to_value(neo_js_callable_t self) {
  return neo_js_object_to_value(&self->super);
}