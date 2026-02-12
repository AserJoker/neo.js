#include "neojs/runtime/promise.h"
#include "neojs/core/allocator.h"
#include "neojs/core/list.h"
#include "neojs/engine/context.h"
#include "neojs/engine/exception.h"
#include "neojs/engine/handle.h"
#include "neojs/engine/runtime.h"
#include "neojs/engine/value.h"
#include "neojs/engine/variable.h"
#include "neojs/runtime/constant.h"

static void neo_js_promise_dispose(neo_allocator_t allocator,
                                   neo_js_promise_t promise) {
  neo_allocator_free(allocator, promise->onfulfilled);
  neo_allocator_free(allocator, promise->onrejected);
}

neo_js_promise_t neo_create_js_promise(neo_allocator_t allocator) {
  neo_js_promise_t promise = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_promise_t), neo_js_promise_dispose);
  promise->value = NULL;
  promise->onfulfilled = neo_create_list(allocator, NULL);
  promise->onrejected = neo_create_list(allocator, NULL);
  return promise;
}

NEO_JS_CFUNCTION(neo_js_promise_on_then) {
  neo_js_variable_t value = neo_js_context_load(ctx, "value");
  neo_js_variable_t resolve = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t reject = neo_js_context_get_argument(ctx, argc, argv, 1);
  neo_js_variable_t then = neo_js_variable_get_field(
      value, ctx, neo_js_context_create_string(ctx, u"then"));
  neo_js_variable_t args[] = {resolve, reject};
  neo_js_variable_call(then, ctx, value, 2, args);
  return neo_js_context_get_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_promise_resolve) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  if (value->value->type >= NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t then = neo_js_variable_get_field(
        value, ctx, neo_js_context_create_string(ctx, u"then"));
    if (then->value->type == NEO_JS_TYPE_FUNCTION) {
      neo_js_variable_t on_then =
          neo_js_context_create_cfunction(ctx, neo_js_promise_on_then, NULL);
      neo_js_variable_set_closure(on_then, ctx, "value", value);
      return neo_js_variable_construct(constant->promise_class, ctx, 1,
                                       &on_then);
    }
  }
  neo_js_variable_t pro = neo_js_context_create_promise(ctx);
  neo_js_promise_t promise = neo_js_variable_get_opaque(pro, ctx, "promise");
  promise->value = value->value;
  neo_js_handle_add_parent(&promise->value->handle, &pro->value->handle);
  return pro;
}
NEO_JS_CFUNCTION(neo_js_promise_reject) {
  neo_js_variable_t pro = neo_js_context_create_promise(ctx);
  neo_js_promise_t promise = neo_js_variable_get_opaque(pro, ctx, "promise");
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  value = neo_js_context_create_exception(ctx, value);
  promise->value = value->value;
  neo_js_handle_add_parent(&promise->value->handle, &pro->value->handle);
  return pro;
}

NEO_JS_CFUNCTION(neo_js_promise_callback) {
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_promise_t promise = neo_js_variable_get_opaque(self, ctx, "promise");
  if (promise->value) {
    return neo_js_context_get_undefined(ctx);
  }
  promise->value = value->value;
  neo_js_value_add_parent(promise->value, self->value);
  if (promise->value->type == NEO_JS_TYPE_EXCEPTION) {
    neo_js_variable_t error = neo_js_context_create_variable(ctx, value->value);
    for (neo_list_node_t it = neo_list_get_first(promise->onrejected);
         it != neo_list_get_tail(promise->onrejected);
         it = neo_list_node_next(it)) {
      neo_js_value_t fn = neo_list_node_get(it);
      neo_js_variable_t callback = neo_js_context_create_variable(ctx, fn);
      neo_js_variable_call(callback, ctx, neo_js_context_get_undefined(ctx), 1,
                           &error);
    }
  } else {
    for (neo_list_node_t it = neo_list_get_first(promise->onfulfilled);
         it != neo_list_get_tail(promise->onfulfilled);
         it = neo_list_node_next(it)) {
      neo_js_value_t fn = neo_list_node_get(it);
      neo_js_variable_t callback = neo_js_context_create_variable(ctx, fn);
      neo_js_variable_call(callback, ctx, neo_js_context_get_undefined(ctx), 1,
                           &value);
    }
  }
  return neo_js_context_get_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_promise_callback_onrejected) {
  neo_js_variable_t error = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t exception = neo_js_context_create_exception(ctx, error);
  return neo_js_promise_callback(ctx, self, 1, &exception);
}

NEO_JS_CFUNCTION(neo_js_promise_task) {
  neo_js_variable_t value = neo_js_context_load(ctx, "value");
  neo_js_variable_t callback = neo_js_context_load(ctx, "callback");
  neo_js_variable_call(callback, ctx, neo_js_context_get_undefined(ctx), 1,
                       &value);
  return neo_js_context_get_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_promise_callback_resolve) {
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_promise_t promise = neo_js_variable_get_opaque(self, ctx, "promise");
  if (promise->value) {
    return neo_js_context_get_undefined(ctx);
  }
  neo_js_variable_t callback =
      neo_js_context_create_cfunction(ctx, neo_js_promise_callback, NULL);
  neo_js_variable_set_bind(callback, ctx, self);
  neo_js_variable_t onrejected = neo_js_context_create_cfunction(
      ctx, neo_js_promise_callback_onrejected, NULL);
  neo_js_variable_set_bind(onrejected, ctx, self);
  if (value->value->type >= NEO_JS_TYPE_OBJECT) {
    neo_js_variable_t then = neo_js_variable_get_field(
        value, ctx, neo_js_context_create_string(ctx, u"then"));
    if (then->value->type >= NEO_JS_TYPE_FUNCTION) {
      neo_js_variable_t args[] = {callback, onrejected};
      neo_js_variable_call(then, ctx, value, 2, args);
      return neo_js_context_get_undefined(ctx);
    }
  }
  neo_js_variable_t task =
      neo_js_context_create_cfunction(ctx, neo_js_promise_task, NULL);
  neo_js_variable_set_closure(task, ctx, "callback", callback);
  neo_js_variable_set_closure(task, ctx, "value", value);
  neo_js_context_create_micro_task(ctx, task);
  return neo_js_context_get_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_promise_callback_reject) {
  neo_js_variable_t error = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_promise_t promise = neo_js_variable_get_opaque(self, ctx, "promise");
  if (promise->value) {
    return neo_js_context_get_undefined(ctx);
  }
  neo_js_variable_t callback =
      neo_js_context_create_cfunction(ctx, neo_js_promise_callback, NULL);
  neo_js_variable_set_bind(callback, ctx, self);
  neo_js_variable_t value = neo_js_context_create_exception(ctx, error);
  neo_js_variable_t task =
      neo_js_context_create_cfunction(ctx, neo_js_promise_task, NULL);
  neo_js_variable_set_closure(task, ctx, "callback", callback);
  neo_js_variable_set_closure(task, ctx, "value", value);
  neo_js_context_create_micro_task(ctx, task);
  return neo_js_context_get_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_promise_constructor) {
  neo_js_variable_t callback = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_runtime_t runtime = neo_js_context_get_runtime(ctx);
  neo_allocator_t allocator = neo_js_runtime_get_allocator(runtime);
  neo_js_promise_t promise = neo_create_js_promise(allocator);
  neo_js_variable_set_opaque(self, ctx, "promise", promise);
  neo_js_variable_t resolve = neo_js_context_create_cfunction(
      ctx, neo_js_promise_callback_resolve, "resolve");
  neo_js_variable_set_bind(resolve, ctx, self);
  neo_js_variable_t reject = neo_js_context_create_cfunction(
      ctx, neo_js_promise_callback_reject, "reject");
  neo_js_variable_set_bind(reject, ctx, self);
  neo_js_variable_t args[] = {resolve, reject};
  neo_js_variable_t res = neo_js_variable_call(
      callback, ctx, neo_js_context_get_undefined(ctx), 2, args);
  if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
    neo_js_variable_t error = neo_js_context_create_variable(
        ctx, ((neo_js_exception_t)res->value)->error);
    neo_js_promise_callback_reject(ctx, self, 1, &error);
  }
  return self;
}

NEO_JS_CFUNCTION(neo_js_promise_onfulfilled) {
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t resolve = neo_js_context_load(ctx, "resolve");
  neo_js_variable_t reject = neo_js_context_load(ctx, "reject");
  neo_js_variable_t onfulfilled = neo_js_context_load(ctx, "onfulfilled");
  if (onfulfilled->value->type >= NEO_JS_TYPE_FUNCTION) {
    value = neo_js_variable_call(onfulfilled, ctx,
                                 neo_js_context_get_undefined(ctx), 1, &value);
  }
  if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
    neo_js_exception_t exception = (neo_js_exception_t)value->value;
    neo_js_variable_t error =
        neo_js_context_create_variable(ctx, exception->error);
    neo_js_variable_call(reject, ctx, neo_js_context_get_undefined(ctx), 1,
                         &error);
  } else {
    neo_js_variable_call(resolve, ctx, neo_js_context_get_undefined(ctx), 1,
                         &value);
  }
  return neo_js_context_get_undefined(ctx);
}
NEO_JS_CFUNCTION(neo_js_promise_onrejected) {
  neo_js_variable_t value = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t resolve = neo_js_context_load(ctx, "resolve");
  neo_js_variable_t reject = neo_js_context_load(ctx, "reject");
  neo_js_variable_t onrejected = neo_js_context_load(ctx, "onrejected");
  if (onrejected->value->type >= NEO_JS_TYPE_FUNCTION) {
    neo_js_exception_t exception = (neo_js_exception_t)value->value;
    neo_js_variable_t error =
        neo_js_context_create_variable(ctx, exception->error);
    value = neo_js_variable_call(onrejected, ctx,
                                 neo_js_context_get_undefined(ctx), 1, &error);
  }
  if (value->value->type == NEO_JS_TYPE_EXCEPTION) {
    neo_js_exception_t exception = (neo_js_exception_t)value->value;
    neo_js_variable_t error =
        neo_js_context_create_variable(ctx, exception->error);
    neo_js_variable_call(reject, ctx, neo_js_context_get_undefined(ctx), 1,
                         &error);
  } else {
    neo_js_variable_call(resolve, ctx, neo_js_context_get_undefined(ctx), 1,
                         &value);
  }
  return neo_js_context_get_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_promise_transform) {
  neo_js_variable_t resolve = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t reject = neo_js_context_get_argument(ctx, argc, argv, 1);
  neo_js_variable_t onfulfilled = neo_js_context_load(ctx, "onfulfilled");
  neo_js_variable_t onrejected = neo_js_context_load(ctx, "onrejected");
  neo_js_promise_t promise = neo_js_variable_get_opaque(self, ctx, "promise");
  if (!promise) {
    neo_js_variable_t message = neo_js_context_create_string(
        ctx, u"Promise.then called on non-promise object");
    neo_js_variable_t type_error_class =
        neo_js_context_get_constant(ctx)->type_error_class;
    neo_js_variable_t error =
        neo_js_variable_construct(type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_variable_t onfulfilled_callback =
      neo_js_context_create_cfunction(ctx, neo_js_promise_onfulfilled, NULL);
  neo_js_variable_set_closure(onfulfilled_callback, ctx, "resolve", resolve);
  neo_js_variable_set_closure(onfulfilled_callback, ctx, "reject", reject);
  neo_js_variable_set_closure(onfulfilled_callback, ctx, "onfulfilled",
                              onfulfilled);
  neo_js_variable_set_bind(onfulfilled_callback, ctx, self);
  neo_js_variable_t onrejected_callback =
      neo_js_context_create_cfunction(ctx, neo_js_promise_onrejected, NULL);
  neo_js_variable_set_closure(onrejected_callback, ctx, "resolve", resolve);
  neo_js_variable_set_closure(onrejected_callback, ctx, "reject", reject);
  neo_js_variable_set_closure(onrejected_callback, ctx, "onrejected",
                              onrejected);
  neo_js_variable_set_bind(onrejected_callback, ctx, self);
  if (promise->value) {
    neo_list_push(promise->onfulfilled, onfulfilled_callback->value);
    neo_js_value_add_parent(onfulfilled_callback->value, self->value);
    neo_list_push(promise->onrejected, onrejected_callback->value);
    neo_js_value_add_parent(onrejected_callback->value, self->value);
    if (promise->value->type == NEO_JS_TYPE_EXCEPTION) {
      neo_js_variable_t value =
          neo_js_context_create_variable(ctx, promise->value);
      neo_js_variable_t task =
          neo_js_context_create_cfunction(ctx, neo_js_promise_task, NULL);
      neo_js_variable_set_closure(task, ctx, "callback", onrejected_callback);
      neo_js_variable_set_closure(task, ctx, "value", value);
      neo_js_context_create_micro_task(ctx, task);
    } else {
      neo_js_variable_t value =
          neo_js_context_create_variable(ctx, promise->value);
      neo_js_variable_t task =
          neo_js_context_create_cfunction(ctx, neo_js_promise_task, NULL);
      neo_js_variable_set_closure(task, ctx, "callback", onfulfilled_callback);
      neo_js_variable_set_closure(task, ctx, "value", value);
      neo_js_context_create_micro_task(ctx, task);
    }
  } else {
    neo_list_push(promise->onfulfilled, onfulfilled_callback->value);
    neo_js_value_add_parent(onfulfilled_callback->value, self->value);
    neo_list_push(promise->onrejected, onrejected_callback->value);
    neo_js_value_add_parent(onrejected_callback->value, self->value);
  }
  return neo_js_context_get_undefined(ctx);
}

NEO_JS_CFUNCTION(neo_js_promise_then) {
  neo_js_promise_t promise = neo_js_variable_get_opaque(self, ctx, "promise");
  if (!promise) {
    neo_js_variable_t message = neo_js_context_create_string(
        ctx, u"Promise.then called on non-promise object");
    neo_js_variable_t type_error_class =
        neo_js_context_get_constant(ctx)->type_error_class;
    neo_js_variable_t error =
        neo_js_variable_construct(type_error_class, ctx, 1, &message);
    return neo_js_context_create_exception(ctx, error);
  }
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  neo_js_variable_t onfulfilled =
      neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t onrejected =
      neo_js_context_get_argument(ctx, argc, argv, 1);
  neo_js_variable_t transform =
      neo_js_context_create_cfunction(ctx, neo_js_promise_transform, NULL);
  neo_js_variable_set_closure(transform, ctx, "onfulfilled", onfulfilled);
  neo_js_variable_set_closure(transform, ctx, "onrejected", onrejected);
  neo_js_variable_set_bind(transform, ctx, self);
  return neo_js_variable_construct(constant->promise_class, ctx, 1, &transform);
}
NEO_JS_CFUNCTION(neo_js_promise_catch) {
  neo_js_variable_t onreject = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t args[] = {neo_js_context_get_undefined(ctx), onreject};
  return neo_js_promise_then(ctx, self, 2, args);
}
NEO_JS_CFUNCTION(neo_js_promise_finally) {
  neo_js_variable_t callback = neo_js_context_get_argument(ctx, argc, argv, 0);
  neo_js_variable_t args[] = {callback, callback};
  return neo_js_promise_then(ctx, self, 2, args);
}
void neo_initialize_js_promise(neo_js_context_t ctx) {
  neo_js_constant_t constant = neo_js_context_get_constant(ctx);
  constant->promise_class = neo_js_context_create_cfunction(
      ctx, neo_js_promise_constructor, "Promise");
  constant->promise_prototype = neo_js_variable_get_field(
      constant->promise_class, ctx, constant->key_prototype);
  NEO_JS_DEF_METHOD(ctx, constant->promise_class, "resolve",
                    neo_js_promise_resolve);
  NEO_JS_DEF_METHOD(ctx, constant->promise_class, "reject",
                    neo_js_promise_reject);
  NEO_JS_DEF_METHOD(ctx, constant->promise_prototype, "then",
                    neo_js_promise_then);
  NEO_JS_DEF_METHOD(ctx, constant->promise_prototype, "catch",
                    neo_js_promise_catch);
  NEO_JS_DEF_METHOD(ctx, constant->promise_prototype, "finally",
                    neo_js_promise_finally);
}