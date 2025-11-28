#ifndef _H_NEO_ENGINE_TASK_
#define _H_NEO_ENGINE_TASK_

#include "core/allocator.h"
#include "engine/value.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct _neo_js_task_t {
  neo_js_value_t callee;
  int64_t idx;
  int64_t time;
  int64_t start;
  bool keep;
};
typedef struct _neo_js_task_t *neo_js_task_t;
neo_js_task_t neo_create_js_task(neo_allocator_t allocator, int64_t idx,
                                 neo_js_value_t callee, int64_t time,
                                 bool keep);
#ifdef __cplusplus
}
#endif
#endif