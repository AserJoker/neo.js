#include "engine/std/promise.h"
#include "core/allocator.h"
#include "core/list.h"
#include "engine/basetype/callable.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/variable.h"

typedef struct _neo_js_promise_t *neo_js_promise_t;
struct _neo_js_promise_t {
  enum {
    NEO_PROMISE_PENDDING,
    NEO_PROMISE_FULFILLED,
    NEO_PROMISE_REJECTED
  } status;
  neo_list_t on_fulfilled_callbacks;
  neo_list_t on_rejected_callbacks;
  neo_js_handle_t value;
  neo_js_handle_t error;
  neo_js_context_t ctx;
};

static neo_js_variable_t
neo_js_promise_resolve_callback(neo_js_context_t ctx, neo_js_variable_t self,
                                uint32_t argc, neo_js_variable_t *argv) {
  neo_js_variable_t resolve = argv[0];
  neo_js_variable_t reject = argv[1];
  neo_js_variable_t value = neo_js_context_load_variable(ctx, L"value");
  if (neo_js_variable_get_type(value)->kind == NEO_JS_TYPE_ERROR) {
    neo_js_context_call(ctx, reject, neo_js_context_create_undefined(ctx), 1,
                        &value);
  } else {
    neo_js_context_call(ctx, resolve, neo_js_context_create_undefined(ctx), 1,
                        &value);
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_promise_resolve(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  neo_js_variable_t value = NULL;
  if (argc) {
    value = argv[0];
  } else {
    value = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t callback = neo_js_context_create_cfunction(
      ctx, NULL, neo_js_promise_resolve_callback);
  neo_js_callable_set_closure(ctx, callback, L"value", value);
  return neo_js_context_construct(
      ctx, neo_js_context_get_std(ctx).promise_constructor, 1, &callback);
}

neo_js_variable_t neo_js_promise_reject(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv) {
  neo_js_variable_t value = NULL;
  if (argc) {
    value = argv[0];
  } else {
    value = neo_js_context_create_undefined(ctx);
  }
  neo_js_variable_t error = neo_js_context_create_error(ctx, value);
  neo_js_variable_t callback = neo_js_context_create_cfunction(
      ctx, NULL, neo_js_promise_resolve_callback);
  neo_js_callable_set_closure(ctx, callback, L"value", error);
  return neo_js_context_construct(
      ctx, neo_js_context_get_std(ctx).promise_constructor, 1, &callback);
}

static void neo_js_promise_dispose(neo_allocator_t allocator,
                                   neo_js_promise_t promise) {
  if (promise->error) {
    if (neo_list_get_size(promise->on_rejected_callbacks) == 0) {
      neo_js_variable_t error =
          neo_js_context_create_variable(promise->ctx, promise->error, NULL);
      error = neo_js_context_to_string(promise->ctx, error);
      fprintf(stderr, "Uncaught promisify %ls\n",
              neo_js_variable_to_string(error)->string);
    }
  }
  neo_allocator_free(allocator, promise->on_fulfilled_callbacks);
  neo_allocator_free(allocator, promise->on_rejected_callbacks);
}
static neo_js_promise_t neo_create_js_promise(neo_allocator_t allocator,
                                              neo_js_context_t ctx) {
  neo_js_promise_t promise = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_promise_t), neo_js_promise_dispose);
  promise->status = NEO_PROMISE_PENDDING;
  promise->on_fulfilled_callbacks = neo_create_list(allocator, NULL);
  promise->on_rejected_callbacks = neo_create_list(allocator, NULL);
  promise->value = NULL;
  promise->error = NULL;
  promise->ctx = ctx;
  return promise;
}

static neo_js_variable_t neo_js_resolve(neo_js_context_t ctx,
                                        neo_js_variable_t self, uint32_t argc,
                                        neo_js_variable_t *argv) {
  neo_js_promise_t promise =
      neo_js_context_get_opaque(ctx, self, L"[[promise]]");
  if (promise->status == NEO_PROMISE_PENDDING) {
    neo_js_variable_t resolve =
        neo_js_context_get_internal(ctx, self, L"[[resolve]]");
    neo_js_variable_t reject =
        neo_js_context_get_internal(ctx, self, L"[[reject]]");
    neo_js_variable_t value = NULL;
    if (argc) {
      value = argv[0];
    } else {
      value = neo_js_context_create_undefined(ctx);
    }
    if (neo_js_context_is_thenable(ctx, value)) {
      neo_js_variable_t then = neo_js_context_get_field(
          ctx, value, neo_js_context_create_string(ctx, L"then"));
      neo_js_variable_t argv[] = {resolve, reject};
      neo_js_variable_t error = neo_js_context_call(ctx, then, value, 2, argv);
      if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
        return error;
      }
    } else {
      promise->status = NEO_PROMISE_FULFILLED;
      promise->value = neo_js_variable_get_handle(value);
      neo_js_handle_add_parent(promise->value,
                               neo_js_variable_get_handle(self));
      for (neo_list_node_t it =
               neo_list_get_first(promise->on_fulfilled_callbacks);
           it != neo_list_get_tail(promise->on_fulfilled_callbacks);
           it = neo_list_node_next(it)) {
        neo_js_handle_t hcallback = neo_list_node_get(it);
        neo_js_variable_t callback =
            neo_js_context_create_variable(ctx, hcallback, NULL);
        neo_js_context_create_micro_task(ctx, callback,
                                         neo_js_context_create_undefined(ctx),
                                         1, &value, 0, false);
      }
    }
  }
  return neo_js_context_create_undefined(ctx);
}

static neo_js_variable_t neo_js_reject(neo_js_context_t ctx,
                                       neo_js_variable_t self, uint32_t argc,
                                       neo_js_variable_t *argv) {
  neo_js_promise_t promise =
      neo_js_context_get_opaque(ctx, self, L"[[promise]]");
  if (promise->status == NEO_PROMISE_PENDDING) {
    promise->status = NEO_PROMISE_REJECTED;
    neo_js_variable_t value = NULL;
    if (argc) {
      value = argv[0];
    } else {
      value = neo_js_context_create_undefined(ctx);
    }
    promise->error = neo_js_variable_get_handle(value);
    neo_js_handle_add_parent(promise->error, neo_js_variable_get_handle(self));
    for (neo_list_node_t it =
             neo_list_get_first(promise->on_rejected_callbacks);
         it != neo_list_get_tail(promise->on_rejected_callbacks);
         it = neo_list_node_next(it)) {
      neo_js_handle_t hcallback = neo_list_node_get(it);
      neo_js_variable_t callback =
          neo_js_context_create_variable(ctx, hcallback, NULL);
      neo_js_context_create_micro_task(ctx, callback,
                                       neo_js_context_create_undefined(ctx), 1,
                                       &value, 0, false);
    }
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_promise_constructor(neo_js_context_t ctx,
                                             neo_js_variable_t self,
                                             uint32_t argc,
                                             neo_js_variable_t *argv) {
  if (neo_js_context_get_call_type(ctx) != NEO_JS_CONSTRUCT_CALL) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L"Promise constructor cannot be invoked without 'new'");
  }
  if (argc < 1 ||
      neo_js_variable_get_type(argv[0])->kind < NEO_JS_TYPE_CALLABLE) {
    return neo_js_context_create_simple_error(
        ctx, NEO_JS_ERROR_TYPE,
        L" Promise resolver undefined is not a function");
  }
  neo_allocator_t allocator = neo_js_context_get_allocator(ctx);
  neo_js_promise_t promise = neo_create_js_promise(allocator, ctx);
  neo_js_variable_t resolve =
      neo_js_context_create_cfunction(ctx, L"resolve", neo_js_resolve);
  neo_js_callable_set_bind(ctx, resolve, self);
  neo_js_context_set_internal(ctx, self, L"[[resolve]]", resolve);
  neo_js_variable_t reject =
      neo_js_context_create_cfunction(ctx, L"reject", neo_js_reject);
  neo_js_callable_set_bind(ctx, reject, self);
  neo_js_context_set_internal(ctx, self, L"[[reject]]", reject);
  neo_js_context_set_opaque(ctx, self, L"[[promise]]", promise);
  neo_js_variable_t resolver = argv[0];
  neo_js_variable_t args[] = {resolve, reject};
  neo_js_variable_t error = neo_js_context_call(
      ctx, resolver, neo_js_context_create_undefined(ctx), 2, args);
  if (neo_js_variable_get_type(error)->kind == NEO_JS_TYPE_ERROR) {
    return error;
  }
  return neo_js_context_create_undefined(ctx);
}
static neo_js_variable_t neo_js_packed_resolve(neo_js_context_t ctx,
                                               neo_js_variable_t self,
                                               uint32_t argc,
                                               neo_js_variable_t *argv) {
  neo_js_variable_t on_fulfilled =
      neo_js_context_load_variable(ctx, L"onFulfilled");
  neo_js_variable_t resolve = neo_js_context_load_variable(ctx, L"resolve");
  neo_js_variable_t reject = neo_js_context_load_variable(ctx, L"reject");
  if (neo_js_variable_get_type(on_fulfilled)->kind >= NEO_JS_TYPE_CALLABLE) {
    neo_js_variable_t result = neo_js_context_call(
        ctx, on_fulfilled, neo_js_context_create_undefined(ctx), argc, argv);
    if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
      return neo_js_context_call(
          ctx, reject, neo_js_context_create_undefined(ctx), 1, &result);
    } else {
      return neo_js_context_call(
          ctx, resolve, neo_js_context_create_undefined(ctx), 1, &result);
    }
  } else {
    return neo_js_context_call(
        ctx, resolve, neo_js_context_create_undefined(ctx), argc, argv);
  }
}

static neo_js_variable_t neo_js_packed_reject(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              uint32_t argc,
                                              neo_js_variable_t *argv) {
  neo_js_variable_t on_rejected =
      neo_js_context_load_variable(ctx, L"onRejected");
  neo_js_variable_t resolve = neo_js_context_load_variable(ctx, L"resolve");
  neo_js_variable_t reject = neo_js_context_load_variable(ctx, L"reject");
  if (neo_js_variable_get_type(on_rejected)->kind >= NEO_JS_TYPE_CALLABLE) {
    neo_js_variable_t result = neo_js_context_call(
        ctx, on_rejected, neo_js_context_create_undefined(ctx), argc, argv);
    if (neo_js_variable_get_type(result)->kind == NEO_JS_TYPE_ERROR) {
      return neo_js_context_call(
          ctx, reject, neo_js_context_create_undefined(ctx), 1, &result);
    } else {
      return neo_js_context_call(
          ctx, resolve, neo_js_context_create_undefined(ctx), 1, &result);
    }
  } else {
    return neo_js_context_call(
        ctx, resolve, neo_js_context_create_undefined(ctx), argc, argv);
  }
}

static neo_js_variable_t neo_js_resolver(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  neo_js_variable_t on_fulfilled =
      neo_js_context_load_variable(ctx, L"onFulfilled");
  neo_js_variable_t on_rejected =
      neo_js_context_load_variable(ctx, L"onRejected");
  neo_js_variable_t resolve = argv[0];
  neo_js_variable_t reject = argv[1];
  neo_js_variable_t packed_resolve =
      neo_js_context_create_cfunction(ctx, L"", neo_js_packed_resolve);
  neo_js_variable_t packed_reject =
      neo_js_context_create_cfunction(ctx, L"", neo_js_packed_reject);
  neo_js_callable_set_closure(ctx, packed_resolve, L"onFulfilled",
                              on_fulfilled);
  neo_js_callable_set_closure(ctx, packed_resolve, L"resolve", resolve);
  neo_js_callable_set_closure(ctx, packed_resolve, L"reject", reject);

  neo_js_callable_set_closure(ctx, packed_reject, L"onRejected", on_rejected);
  neo_js_callable_set_closure(ctx, packed_reject, L"resolve", resolve);
  neo_js_callable_set_closure(ctx, packed_reject, L"reject", reject);

  neo_js_promise_t promise =
      neo_js_context_get_opaque(ctx, self, L"[[promise]]");
  neo_js_handle_t hpromise = neo_js_variable_get_handle(self);
  if (promise->status == NEO_PROMISE_PENDDING) {
    if (neo_js_variable_get_type(on_fulfilled)->kind >= NEO_JS_TYPE_CALLABLE) {
      neo_js_handle_t hfulfilled = neo_js_variable_get_handle(on_fulfilled);
      neo_list_push(promise->on_fulfilled_callbacks, hfulfilled);
      neo_js_handle_add_parent(hfulfilled, hpromise);
    }
    if (neo_js_variable_get_type(on_rejected)->kind >= NEO_JS_TYPE_CALLABLE) {
      neo_js_handle_t hrejected = neo_js_variable_get_handle(on_rejected);
      neo_list_push(promise->on_rejected_callbacks, hrejected);
      neo_js_handle_add_parent(hrejected, hpromise);
    }
  } else if (promise->status == NEO_PROMISE_FULFILLED) {
    neo_js_variable_t value =
        neo_js_context_create_variable(ctx, promise->value, NULL);
    neo_js_context_create_micro_task(ctx, packed_resolve,
                                     neo_js_context_create_undefined(ctx), 1,
                                     &value, 0, false);
  } else if (promise->status == NEO_PROMISE_REJECTED) {
    neo_js_variable_t value =
        neo_js_context_create_variable(ctx, promise->error, NULL);
    neo_js_context_create_micro_task(ctx, packed_reject,
                                     neo_js_context_create_undefined(ctx), 1,
                                     &value, 0, false);
  }
  return neo_js_context_create_undefined(ctx);
}

neo_js_variable_t neo_js_promise_then(neo_js_context_t ctx,
                                      neo_js_variable_t self, uint32_t argc,
                                      neo_js_variable_t *argv) {
  neo_js_variable_t on_fulfilled = NULL;
  neo_js_variable_t on_rejected = NULL;
  if (argc > 0) {
    on_fulfilled = argv[0];
  }
  if (argc > 1) {
    on_rejected = argv[1];
  }
  if (!on_fulfilled) {
    on_fulfilled = neo_js_context_create_null(ctx);
  }
  if (!on_rejected) {
    on_rejected = neo_js_context_create_null(ctx);
  }
  neo_js_variable_t resolver =
      neo_js_context_create_cfunction(ctx, NULL, neo_js_resolver);
  neo_js_callable_set_closure(ctx, resolver, L"onFulfilled", on_fulfilled);
  neo_js_callable_set_closure(ctx, resolver, L"onRejected", on_rejected);
  neo_js_callable_set_bind(ctx, resolver, self);
  neo_js_variable_t promise = neo_js_context_get_std(ctx).promise_constructor;
  return neo_js_context_construct(ctx, promise, 1, &resolver);
}

neo_js_variable_t neo_js_promise_catch(neo_js_context_t ctx,
                                       neo_js_variable_t self, uint32_t argc,
                                       neo_js_variable_t *argv) {
  if (argc > 0) {
    neo_js_variable_t args[] = {neo_js_context_create_undefined(ctx), argv[0]};
    return neo_js_promise_then(ctx, self, 2, args);
  }
  return neo_js_promise_then(ctx, self, 0, NULL);
}
neo_js_variable_t neo_js_promise_finally(neo_js_context_t ctx,
                                         neo_js_variable_t self, uint32_t argc,
                                         neo_js_variable_t *argv) {
  if (argc > 0) {
    neo_js_variable_t args[] = {argv[0], argv[0]};
    return neo_js_promise_then(ctx, self, 2, args);
  }
  return neo_js_promise_then(ctx, self, 0, NULL);
}