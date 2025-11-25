#include "engine/signal.h"
#include "core/allocator.h"
#include "engine/value.h"

static void neo_js_signal_dispose(neo_allocator_t allocator,
                                  neo_js_signal_t signal) {
  neo_deinit_js_signal(signal, allocator);
}

neo_js_signal_t neo_create_js_signal(neo_allocator_t allocator, uint32_t type,
                                     const void *msg) {
  neo_js_signal_t signal = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_signal_t), neo_js_signal_dispose);
  neo_init_js_signal(signal, allocator, type, msg);
  return signal;
}

void neo_init_js_signal(neo_js_signal_t self, neo_allocator_t allocaotr,
                        uint32_t type, const void *msg) {
  neo_init_js_value(&self->super, allocaotr, NEO_JS_TYPE_SIGNAL);
  self->type = type;
  self->msg = msg;
}

void neo_deinit_js_signal(neo_js_signal_t self, neo_allocator_t allocaotr) {
  neo_deinit_js_value(&self->super, allocaotr);
}

neo_js_value_t neo_js_signal_to_value(neo_js_signal_t self) {
  return &self->super;
}