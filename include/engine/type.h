#ifndef _H_NEO_ENGINE_TYPE_
#define _H_NEO_ENGINE_TYPE_
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_context_t *neo_js_context_t;
typedef struct _neo_js_variable_t *neo_js_variable_t;

typedef const wchar_t *(*neo_js_typeof_fn_t)(neo_js_context_t ctx,
                                             neo_js_variable_t self);

typedef neo_js_variable_t (*neo_js_void_fn_t)(neo_js_context_t ctx,
                                              neo_js_variable_t self);

typedef neo_js_variable_t (*neo_js_add_fn_t)(neo_js_context_t ctx,
                                             neo_js_variable_t self,
                                             neo_js_variable_t another);

typedef neo_js_variable_t (*neo_js_to_string_fn_t)(neo_js_context_t ctx,
                                                   neo_js_variable_t self);

typedef neo_js_variable_t (*neo_js_to_boolean_fn_t)(neo_js_context_t ctx,
                                                    neo_js_variable_t self);

typedef neo_js_variable_t (*neo_js_to_number_fn_t)(neo_js_context_t ctx,
                                                   neo_js_variable_t self);

typedef neo_js_variable_t (*neo_js_to_object_fn_t)(neo_js_context_t ctx,
                                                   neo_js_variable_t self);

typedef neo_js_variable_t (*neo_js_to_primitive_fn_t)(neo_js_context_t ctx,
                                                      neo_js_variable_t self);

typedef neo_js_variable_t (*neo_js_get_field_fn_t)(neo_js_context_t ctx,
                                                   neo_js_variable_t object,
                                                   neo_js_variable_t field);

typedef neo_js_variable_t (*neo_js_del_field_fn_t)(neo_js_context_t ctx,
                                                   neo_js_variable_t object,
                                                   neo_js_variable_t field);

typedef neo_js_variable_t (*neo_js_set_field_fn_t)(neo_js_context_t ctx,
                                                   neo_js_variable_t object,
                                                   neo_js_variable_t field,
                                                   neo_js_variable_t value);

typedef bool (*neo_js_is_equal_fn_t)(neo_js_context_t ctx,
                                     neo_js_variable_t self,
                                     neo_js_variable_t another);

typedef void (*neo_js_copy_fn_t)(neo_js_context_t ctx, neo_js_variable_t self,
                                 neo_js_variable_t target);

typedef enum _neo_js_type_kind_t {
  NEO_TYPE_UNINITIALIZE,
  NEO_TYPE_ERROR,
  NEO_TYPE_INTERRUPT,
  NEO_TYPE_NULL,
  NEO_TYPE_UNDEFINED,
  NEO_TYPE_NUMBER,
  NEO_TYPE_STRING,
  NEO_TYPE_BOOLEAN,
  NEO_TYPE_SYMBOL,
  NEO_TYPE_OBJECT,
  NEO_TYPE_ARRAY,
  NEO_TYPE_CALLABLE,
  NEO_TYPE_CFUNCTION,
  NEO_TYPE_FUNCTION,
} neo_js_type_kind_t;

typedef struct _neo_js_type_t {
  neo_js_type_kind_t kind;
  neo_js_typeof_fn_t typeof_fn;
  neo_js_to_string_fn_t to_string_fn;
  neo_js_to_boolean_fn_t to_boolean_fn;
  neo_js_to_number_fn_t to_number_fn;
  neo_js_to_primitive_fn_t to_primitive_fn;
  neo_js_to_object_fn_t to_object_fn;
  neo_js_get_field_fn_t get_field_fn;
  neo_js_set_field_fn_t set_field_fn;
  neo_js_del_field_fn_t del_field_fn;
  neo_js_is_equal_fn_t is_equal_fn;
  neo_js_copy_fn_t copy_fn;
} *neo_js_type_t;

typedef neo_js_variable_t (*neo_js_cfunction_fn_t)(neo_js_context_t ctx,
                                                   neo_js_variable_t self,
                                                   uint32_t argc,
                                                   neo_js_variable_t *argv);

#ifdef __cplusplus
}
#endif
#endif