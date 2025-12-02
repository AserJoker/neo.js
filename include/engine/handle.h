#ifndef _H_NEO_ENGINE_HANDLE_
#define _H_NEO_ENGINE_HANDLE_
#include "core/allocator.h"
#include "core/list.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_handle_t *neo_js_handle_t;
struct _neo_js_handle_t {
  neo_list_t parent;
  neo_list_t children;
  bool is_alive;
  bool is_check;
  bool is_disposed;
  bool is_root;
  uint32_t age;
};
typedef void (*neo_js_handle_on_gc_fn_t)(neo_allocator_t allocator,
                                         neo_js_handle_t handle, void *ctx);
void neo_init_js_handle(neo_js_handle_t self, neo_allocator_t allocator);
void neo_deinit_js_handle(neo_js_handle_t self, neo_allocator_t allocator);
void neo_js_handle_add_parent(neo_js_handle_t self, neo_js_handle_t parent);
void neo_js_handle_remove_parent(neo_js_handle_t self, neo_js_handle_t parent);
void neo_js_handle_gc(neo_allocator_t allocator, neo_list_t handles,
                      neo_list_t gclist, neo_js_handle_on_gc_fn_t cb,
                      void *ctx);
#ifdef __cplusplus
}
#endif
#endif