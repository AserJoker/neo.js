#include "neojs/runtime/array_buffer.h"
#include "neojs/core/allocator.h"
#include "neojs/engine/context.h"
#include "neojs/engine/number.h"
#include "neojs/engine/value.h"
#include "neojs/engine/variable.h"
#include "neojs/runtime/constant.h"
#include <string.h>
struct _neo_buffer_data_t {
  void *data;
  size_t byte_length;
  size_t max_byte_length;
  bool resizable;
};
typedef struct _neo_buffer_data_t *neo_buffer_data_t;
static void neo_buffer_data_dispose(neo_allocator_t allocator,
                                    neo_buffer_data_t self) {
  if (self->data) {
    neo_allocator_free(allocator, self->data);
  }
}
static neo_buffer_data_t neo_create_buffer_data(neo_allocator_t allocator,
                                                size_t byte_length,
                                                size_t max_byte_length,
                                                bool resizable) {
  neo_buffer_data_t data = neo_allocator_alloc(
      allocator, sizeof(struct _neo_buffer_data_t), neo_buffer_data_dispose);
  data->byte_length = byte_length;
  data->max_byte_length = max_byte_length;
  data->resizable = resizable;
  if (max_byte_length) {
    data->data = neo_allocator_alloc(allocator, data->max_byte_length, NULL);
    memset(data->data, 0, data->max_byte_length);
  } else {
    data->data = NULL;
  }
  return data;
}
NEO_JS_CFUNCTION(neo_js_array_buffer_constructor) {
  if (neo_js_context_get_type(ctx) != NEO_JS_CONTEXT_CONSTRUCT) {
    neo_js_variable_t message = neo_js_context_create_cstring(
        ctx, "Constructor ArrayBuffer requires 'new'");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_variable_t byte_length =
      neo_js_context_get_argument(ctx, argc, argv, 0);
  byte_length = neo_js_variable_to_integer(byte_length, ctx);
  if (byte_length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return byte_length;
  }
  double length = ((neo_js_number_t)byte_length->value)->value;
  if (length >= (double)((int64_t)2 << 53) || length < 0) {
    neo_js_variable_t message =
        neo_js_context_create_cstring(ctx, "Invalid array buffer length");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->range_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double max_length = length;
  bool resizable = false;
  if (argc > 1 && argv[1]->value->type >= NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t max_byte_length = neo_js_variable_get_field(
        argv[1], ctx, neo_js_context_create_cstring(ctx, "maxByteLength"));
    max_length = ((neo_js_number_t)max_byte_length->value)->value;
    if (max_length >= (double)((int64_t)2 << 53) || max_length < length) {
      neo_js_variable_t message =
          neo_js_context_create_cstring(ctx, "Invalid array buffer max length");
      neo_js_variable_t error = neo_js_variable_construct(
          neo_js_context_get_constant(ctx)->range_error_class, ctx, 1,
          &message);
      return neo_js_context_create_exception(ctx, error);
    }
    resizable = true;
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_buffer_data_t buffer_data =
      neo_create_buffer_data(allocator, length, max_length, resizable);
  neo_js_variable_set_opaque(self, ctx, "bufferData", buffer_data);
  return self;
}
NEO_JS_CFUNCTION(neo_js_array_buffer_is_view) {
  neo_js_variable_t view = neo_js_context_get_argument(ctx, argc, argv, 0);
  if (view->value->type < NEO_JS_TYPE_OBJECT) {
    return neo_js_context_get_false(ctx);
  }
  neo_js_variable_t buffer = neo_js_variable_get_internel(view, ctx, "buffer");
  if (buffer) {
    if (neo_js_variable_get_opaque(buffer, ctx, "bufferData")) {
      return neo_js_context_get_true(ctx);
    }
  }
  return neo_js_context_get_false(ctx);
}
NEO_JS_CFUNCTION(neo_js_array_buffer_get_species) { return self; }
NEO_JS_CFUNCTION(neo_js_array_buffer_resize) {
  neo_buffer_data_t data = neo_js_variable_get_opaque(self, ctx, "bufferData");
  if (!data) {
    neo_js_variable_t message =
        neo_js_context_format(ctx,
                              "Method ArrayBuffer.prototype.resize "
                              "called on incompatible receiver %v",
                              self);
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  if (!data->resizable) {
    neo_js_variable_t message =
        neo_js_context_create_cstring(ctx, "ArrayBuffer is not resizable");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_variable_t new_byte_length =
      neo_js_context_get_argument(ctx, argc, argv, 0);
  new_byte_length = neo_js_variable_to_integer(new_byte_length, ctx);
  if (new_byte_length->value->type == NEO_JS_TYPE_EXCEPTION) {
    return new_byte_length;
  }
  double len = ((neo_js_number_t)new_byte_length->value)->value;
  if (len > data->max_byte_length || len < 0) {
    neo_js_variable_t message =
        neo_js_context_create_cstring(ctx, "Invalid length parameter");
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->range_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  if (len > data->byte_length) {
    memset((uint8_t *)data->data + data->byte_length, 0,
           len - data->byte_length);
  }
  data->byte_length = len;
  return self;
}
NEO_JS_CFUNCTION(neo_js_array_buffer_slice) {
  neo_buffer_data_t data = neo_js_variable_get_opaque(self, ctx, "bufferData");
  if (!data) {
    neo_js_variable_t message =
        neo_js_context_format(ctx,
                              "Method ArrayBuffer.prototype.slice "
                              "called on incompatible receiver %v",
                              self);
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  double start = 0;
  double end = data->byte_length;
  if (argc > 0) {
    neo_js_variable_t val = neo_js_context_get_argument(ctx, argc, argv, 0);
    val = neo_js_variable_to_integer(val, ctx);
    if (val->value->type == NEO_JS_TYPE_EXCEPTION) {
      return val;
    }
    start = ((neo_js_number_t)val->value)->value;
    if (start < 0) {
      start += data->byte_length;
    }
    if (start < 0) {
      start = 0;
    }
  }
  if (argc > 1) {
    neo_js_variable_t val = neo_js_context_get_argument(ctx, argc, argv, 1);
    val = neo_js_variable_to_integer(val, ctx);
    if (val->value->type == NEO_JS_TYPE_EXCEPTION) {
      return val;
    }
    end = ((neo_js_number_t)val->value)->value;
    if (end < 0) {
      end += data->byte_length;
    }
  }
  if (end <= start || start >= data->byte_length) {
    return neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->array_buffer_class, ctx, 0, NULL);
  }
  neo_js_variable_t constructor = neo_js_variable_get_field(
      self, ctx, neo_js_context_create_cstring(ctx, "constructor"));
  if (constructor->value->type == NEO_JS_TYPE_EXCEPTION) {
    return constructor;
  }
  constructor = neo_js_variable_get_field(
      constructor, ctx, neo_js_context_get_constant(ctx)->symbol_species);
  if (constructor->value->type == NEO_JS_TYPE_EXCEPTION) {
    return constructor;
  }
  neo_js_variable_t length = neo_js_context_create_number(ctx, end - start);
  neo_js_variable_t result =
      neo_js_variable_construct(constructor, ctx, 1, &length);
  neo_buffer_data_t target =
      neo_js_variable_get_opaque(result, ctx, "bufferData");
  memcpy(target->data, (uint8_t *)data->data + (int64_t)start, end - start);
  return result;
}
NEO_JS_CFUNCTION(neo_js_array_buffer_get_byte_length) {
  neo_buffer_data_t data = neo_js_variable_get_opaque(self, ctx, "bufferData");
  if (!data) {
    neo_js_variable_t message =
        neo_js_context_format(ctx,
                              "Method get ArrayBuffer.prototype.byteLength "
                              "called on incompatible receiver %v",
                              self);
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  return neo_js_context_create_number(ctx, data->byte_length);
}
NEO_JS_CFUNCTION(neo_js_array_buffer_get_max_byte_length) {
  neo_buffer_data_t data = neo_js_variable_get_opaque(self, ctx, "bufferData");
  if (!data) {
    neo_js_variable_t message =
        neo_js_context_format(ctx,
                              "Method get ArrayBuffer.prototype.byteLength "
                              "called on incompatible receiver %v",
                              self);
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  return neo_js_context_create_number(ctx, data->max_byte_length);
}
NEO_JS_CFUNCTION(neo_js_array_buffer_get_resizable) {
  neo_buffer_data_t data = neo_js_variable_get_opaque(self, ctx, "bufferData");
  if (!data) {
    neo_js_variable_t message =
        neo_js_context_format(ctx,
                              "Method get ArrayBuffer.prototype.byteLength "
                              "called on incompatible receiver %v",
                              self);
    neo_js_variable_t error = neo_js_variable_construct(
        neo_js_context_get_constant(ctx)->type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  return neo_js_context_create_number(ctx, data->resizable);
}
void neo_initialize_js_array_buffer(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->array_buffer_class = neo_js_context_create_cfunction(
      ctx, neo_js_array_buffer_constructor, "ArrayBuffer");
  neo_js_variable_t prototype = neo_js_variable_get_field(
      constant->array_buffer_class, ctx, constant->key_prototype);
  NEO_JS_DEF_METHOD(ctx, constant->array_buffer_class, "isView",
                    neo_js_array_buffer_is_view);
  neo_js_variable_t func = neo_js_context_create_cfunction(
      ctx, neo_js_array_buffer_get_species, "[Symbol.species]");
  neo_js_variable_def_accessor(constant->array_buffer_class, ctx,
                               constant->symbol_species, func, NULL, true,
                               false);
  NEO_JS_DEF_METHOD(ctx, prototype, "resize", neo_js_array_buffer_resize);
  NEO_JS_DEF_METHOD(ctx, prototype, "slice", neo_js_array_buffer_slice);
  func = neo_js_context_create_cfunction(
      ctx, neo_js_array_buffer_get_byte_length, NULL);
  neo_js_variable_def_accessor(prototype, ctx,
                               neo_js_context_create_cstring(ctx, "byteLength"),
                               func, NULL, true, false);
  func = neo_js_context_create_cfunction(
      ctx, neo_js_array_buffer_get_max_byte_length, NULL);
  neo_js_variable_def_accessor(
      prototype, ctx, neo_js_context_create_cstring(ctx, "maxByteLength"), func,
      NULL, true, false);
  func = neo_js_context_create_cfunction(ctx, neo_js_array_buffer_get_resizable,
                                         NULL);
  neo_js_variable_def_accessor(prototype, ctx,
                               neo_js_context_create_cstring(ctx, "resizable"),
                               func, NULL, true, false);
}