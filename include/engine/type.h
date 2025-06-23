#ifndef _H_NEO_ENGINE_TYPE_
#define _H_NEO_ENGINE_TYPE_
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_engine_context_t *neo_engine_context_t;
typedef struct _neo_engine_variable_t *neo_engine_variable_t;

typedef const wchar_t *(*neo_engine_typeof_fn_t)(neo_engine_context_t ctx,
                                                 neo_engine_variable_t self);

typedef neo_engine_variable_t (*neo_engine_void_fn_t)(
    neo_engine_context_t ctx, neo_engine_variable_t self);

typedef neo_engine_variable_t (*neo_engine_add_fn_t)(
    neo_engine_context_t ctx, neo_engine_variable_t self,
    neo_engine_variable_t another);

typedef neo_engine_variable_t (*neo_engine_to_string_fn_t)(
    neo_engine_context_t ctx, neo_engine_variable_t self);

typedef neo_engine_variable_t (*neo_engine_to_boolean_fn_t)(
    neo_engine_context_t ctx, neo_engine_variable_t self);

typedef neo_engine_variable_t (*neo_engine_to_number_fn_t)(
    neo_engine_context_t ctx, neo_engine_variable_t self);

typedef neo_engine_variable_t (*neo_engine_to_object_fn_t)(
    neo_engine_context_t ctx, neo_engine_variable_t self);

typedef neo_engine_variable_t (*neo_engine_to_primitive_fn_t)(
    neo_engine_context_t ctx, neo_engine_variable_t self);

typedef neo_engine_variable_t (*neo_engine_get_field_fn_t)(
    neo_engine_context_t ctx, neo_engine_variable_t object,
    neo_engine_variable_t field);

typedef neo_engine_variable_t (*neo_engine_del_field_fn_t)(
    neo_engine_context_t ctx, neo_engine_variable_t object,
    neo_engine_variable_t field);

typedef neo_engine_variable_t (*neo_engine_set_field_fn_t)(
    neo_engine_context_t ctx, neo_engine_variable_t object,
    neo_engine_variable_t field, neo_engine_variable_t value);

typedef bool (*neo_engine_is_equal_fn_t)(neo_engine_context_t ctx,
                                         neo_engine_variable_t self,
                                         neo_engine_variable_t another);

typedef void (*neo_engine_copy_fn_t)(neo_engine_context_t ctx,
                                     neo_engine_variable_t self,
                                     neo_engine_variable_t target);

typedef enum _neo_engine_type_kind_t {
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
  NEO_TYPE_FUNCTION,
} neo_engine_type_kind_t;

typedef struct _neo_engine_type_t {
  neo_engine_type_kind_t kind;
  neo_engine_typeof_fn_t typeof_fn;
  neo_engine_to_string_fn_t to_string_fn;
  neo_engine_to_boolean_fn_t to_boolean_fn;
  neo_engine_to_number_fn_t to_number_fn;
  neo_engine_to_primitive_fn_t to_primitive_fn;
  neo_engine_to_object_fn_t to_object_fn;
  neo_engine_get_field_fn_t get_field_fn;
  neo_engine_set_field_fn_t set_field_fn;
  neo_engine_del_field_fn_t del_field_fn;
  neo_engine_is_equal_fn_t is_equal_fn;
  neo_engine_copy_fn_t copy_fn;
} *neo_engine_type_t;

typedef neo_engine_variable_t (*neo_engine_cfunction_fn_t)(
    neo_engine_context_t ctx, neo_engine_variable_t self, uint32_t argc,
    neo_engine_variable_t *argv);

#ifdef __cplusplus
}
#endif
#endif