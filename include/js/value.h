#ifndef _H_NEO_JS_VALUE_
#define _H_NEO_JS_VALUE_
#include "js/type.h"
#ifdef __cplusplus
extern "C" {
#endif
struct _neo_js_value_t {
  neo_js_type_t type;
  uint32_t ref;
};

typedef struct _neo_js_value_t *neo_js_value_t;

#ifdef __cplusplus
}
#endif
#endif