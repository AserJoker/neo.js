#ifndef _H_NEO_ENGINE_TASK_
#define _H_NEO_ENGINE_TASK_

#include "engine/variable.h"
#ifdef __cplusplus
extern "C" {
#endif
struct _neo_js_task_t {
  neo_js_variable_t callee;
  uint64_t time;
  uint64_t start;
  bool keep;
};
typedef struct _neo_js_task_t *neo_js_task_t;

#ifdef __cplusplus
}
#endif
#endif