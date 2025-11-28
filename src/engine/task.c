#include "engine/task.h"
#include "core/allocator.h"
#include "core/clock.h"
static void neo_js_task_dispose(neo_allocator_t allocator, neo_js_task_t task) {
}
neo_js_task_t neo_create_js_task(neo_allocator_t allocator, int64_t idx,
                                 neo_js_value_t callee, int64_t time,
                                 bool keep) {
  neo_js_task_t task = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_task_t), neo_js_task_dispose);
  task->idx = idx;
  task->callee = callee;
  task->time = time;
  task->keep = keep;
  task->start = neo_clock_get_timestamp();
  return task;
}