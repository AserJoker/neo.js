#include "engine/basetype/ref.h"
#include "core/allocator.h"
#include "engine/chunk.h"
#include "engine/context.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/variable.h"
#include <wchar.h>

static const wchar_t *neo_js_ref_typeof(neo_js_context_t ctx,
                                        neo_js_variable_t self) {
  neo_js_variable_t target = neo_js_ref_get_target(ctx, self);
  neo_js_type_t type = neo_js_variable_get_type(target);
  return type->typeof_fn(ctx, target);
}

static neo_js_variable_t neo_js_ref_to_string(neo_js_context_t ctx,
                                              neo_js_variable_t self) {
  neo_js_variable_t target = neo_js_ref_get_target(ctx, self);
  neo_js_type_t type = neo_js_variable_get_type(target);
  return type->to_string_fn(ctx, target);
}

static neo_js_variable_t neo_js_ref_to_boolean(neo_js_context_t ctx,
                                               neo_js_variable_t self) {
  neo_js_variable_t target = neo_js_ref_get_target(ctx, self);
  neo_js_type_t type = neo_js_variable_get_type(target);
  return type->to_boolean_fn(ctx, target);
}

static neo_js_variable_t neo_js_ref_to_number(neo_js_context_t ctx,
                                              neo_js_variable_t self) {
  neo_js_variable_t target = neo_js_ref_get_target(ctx, self);
  neo_js_type_t type = neo_js_variable_get_type(target);
  return type->to_number_fn(ctx, target);
}

static neo_js_variable_t neo_js_ref_to_primitive(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 const wchar_t *hint) {
  neo_js_variable_t target = neo_js_ref_get_target(ctx, self);
  neo_js_type_t type = neo_js_variable_get_type(target);
  return type->to_primitive_fn(ctx, target, hint);
}

static neo_js_variable_t neo_js_ref_to_object(neo_js_context_t ctx,
                                              neo_js_variable_t self) {
  neo_js_variable_t target = neo_js_ref_get_target(ctx, self);
  neo_js_type_t type = neo_js_variable_get_type(target);
  return type->to_object_fn(ctx, target);
}

static neo_js_variable_t neo_js_ref_get_field(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              neo_js_variable_t field,
                                              neo_js_variable_t receiver) {
  neo_js_variable_t target = neo_js_ref_get_target(ctx, self);
  neo_js_type_t type = neo_js_variable_get_type(target);
  return type->get_field_fn(ctx, target, field, receiver);
}

static neo_js_variable_t neo_js_ref_del_field(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              neo_js_variable_t field) {
  neo_js_variable_t target = neo_js_ref_get_target(ctx, self);
  neo_js_type_t type = neo_js_variable_get_type(target);
  return type->del_field_fn(ctx, target, field);
}

static neo_js_variable_t neo_js_ref_set_field(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              neo_js_variable_t field,
                                              neo_js_variable_t value,
                                              neo_js_variable_t receiver) {
  neo_js_variable_t target = neo_js_ref_get_target(ctx, self);
  neo_js_type_t type = neo_js_variable_get_type(target);
  return type->set_field_fn(ctx, target, field, value, receiver);
}

static bool neo_js_ref_is_equal(neo_js_context_t ctx, neo_js_variable_t self,
                                neo_js_variable_t another) {
  neo_js_variable_t target = neo_js_ref_get_target(ctx, self);
  neo_js_type_t type = neo_js_variable_get_type(target);
  return type->is_equal_fn(ctx, target, another);
}

static neo_js_variable_t neo_js_ref_copy(neo_js_context_t ctx,
                                         neo_js_variable_t self,
                                         neo_js_variable_t target2) {
  neo_js_variable_t target = neo_js_ref_get_target(ctx, self);
  neo_js_type_t type = neo_js_variable_get_type(target);
  return type->copy_fn(ctx, target, target2);
}

neo_js_type_t neo_get_js_ref_type() {
  static struct _neo_js_type_t type = {
      NEO_JS_TYPE_REF,       neo_js_ref_typeof,    neo_js_ref_to_string,
      neo_js_ref_to_boolean, neo_js_ref_to_number, neo_js_ref_to_primitive,
      neo_js_ref_to_object,  neo_js_ref_get_field, neo_js_ref_set_field,
      neo_js_ref_del_field,  neo_js_ref_is_equal,  neo_js_ref_copy,
  };
  return &type;
}

static void neo_js_ref_dispose(neo_allocator_t allocator, neo_js_ref_t self) {
  neo_js_value_dispose(allocator, &self->value);
}

neo_js_ref_t neo_create_js_ref(neo_allocator_t allocator) {
  neo_js_ref_t ref = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_ref_t), neo_js_ref_dispose);
  ref->target = NULL;
  neo_js_value_init(allocator, &ref->value);
  ref->value.type = neo_get_js_ref_type();
  return ref;
}

neo_js_ref_t neo_js_value_to_ref(neo_js_value_t value) {
  if (value->type == neo_get_js_ref_type()) {
    return (neo_js_ref_t)value;
  }
  return NULL;
}

neo_js_variable_t neo_js_ref_set_target(neo_js_context_t ctx,
                                        neo_js_variable_t self,
                                        neo_js_variable_t target) {
  neo_js_chunk_t hself = neo_js_variable_get_raw_handle(self);
  neo_js_value_t value = neo_js_chunk_get_value(hself);
  neo_js_ref_t ref = neo_js_value_to_ref(value);
  neo_js_chunk_t htarget = neo_js_variable_get_handle(target);
  neo_js_scope_t scope = neo_js_context_get_scope(ctx);
  neo_js_chunk_t hroot = neo_js_scope_get_root_handle(scope);
  neo_js_chunk_add_parent(htarget, hself);
  if (ref->target) {
    neo_js_chunk_remove_parent(ref->target, hself);
    neo_js_chunk_add_parent(ref->target, hroot);
  }
  ref->target = htarget;
  return self;
}

neo_js_variable_t neo_js_ref_get_target(neo_js_context_t ctx,
                                        neo_js_variable_t self) {
  neo_js_chunk_t hself = neo_js_variable_get_raw_handle(self);
  neo_js_value_t value = neo_js_chunk_get_value(hself);
  neo_js_ref_t ref = neo_js_value_to_ref(value);
  return neo_js_context_create_variable(ctx, ref->target, NULL);
}