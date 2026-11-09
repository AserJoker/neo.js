// ErrorBuiltins.cpp - Error 基类与 NativeError 子类注册实现
//
// bootstrap 顺序（依赖 Object/Function 内建已就绪）：
//   1. Error.prototype（普通对象，原型 = Object.prototype）
//   2. setErrorPrototype —— 此后 createError 自动挂链
//   3. Error 构造函数（prototype 属性指向 Error.prototype，双向 constructor）
//   4. Error.prototype.name / message / toString
//   5. 一般性错误子类：TypeError / RangeError / ReferenceError / SyntaxError / URIError
//      （ES2024 §20.5.6；EvalError 已不在规范中，不实现）
//   6. 注册到 global
//
// 子类原型都是**普通对象**（无 [[ErrorData]] 内部槽，§20.5.6.3），原型链指向
// %Error.prototype%，自有 name 与 constructor。实例由 createError 建好后改挂原型。
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、034（原型-构造函数）、038（Error 基类）

#include "core/ErrorBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <string>
#include <vector>

namespace neo::core {

namespace {

// 取第一个实参并 ToString（ES2024 §20.5.1.1 step 2）：
//   无参或 undefined → 返回 false 且 message 为空（不定义属性）；
//   否则返回 true 并写入 out。转换失败写入 err。
bool messageArgument(IContext* ctx, size_t argc, Value* argv[], std::string& out,
                     Value*& err) {
    err = nullptr;
    out.clear();
    if (argc == 0 || argv[0]->type()->kind() == TypeKind::Undefined) return false;
    Value* s = detail::toStringValue(ctx, argv[0]);
    if (detail::isErrorValue(s)) {
        err = s;
        return false;
    }
    out = static_cast<StringData*>(s->data())->value;
    return true;
}

// Error(message) / new Error(message)（ES2024 §20.5.1.1）：
//   无参或 message 为 undefined → 不定义 message 属性；
//   否则 ToString(message) 后定义。call 与 new 语义相同。
Value* errorCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    std::string message;
    Value* err = nullptr;
    messageArgument(ctx, argc, argv, message, err);
    if (err != nullptr) return err;
    return ctx->createError(message);
}

// 子类构造函数共用的实现（TypeError/RangeError/...）：
//   1. 按 §20.5.6.1 NativeError(message) 取 message；
//   2. createError 建实例（stack 首行用子类名）；
//   3. 把原型从 %Error.prototype% 改挂到子类原型（createError 默认挂基类原型）。
// 子类原型经 global 上的构造函数取（bootstrap 时已建立），避免为 5 个子类
// 各加一个 Context getter。
Value* nativeErrorCtor(IContext* ctx, const char* ctorName, const char* errorName,
                       size_t argc, Value* argv[]) {
    std::string message;
    Value* err = nullptr;
    messageArgument(ctx, argc, argv, message, err);
    if (err != nullptr) return err;

    Value* instance = ctx->createError(message, errorName);
    if (instance == ctx->oomValue()) return instance;

    Value* ctor = ctx->global()->lookup(ctorName);
    if (ctor == nullptr) {
        return detail::typeError(ctx, "internal: missing error constructor");
    }
    Value* proto = ctx->getProperty(ctor, "prototype");
    if (detail::isErrorValue(proto)) return proto;
    Value* r = ctx->setPrototype(instance, proto);
    if (detail::isErrorValue(r)) return r;
    return instance;
}

// AggregateError(errors, message)（ES2024 §20.5.7.1）：
//   1. errors 必须是可迭代对象，IterableToList 收集（失败透传）；
//   2. message 按 NativeError 规则取（undefined 时不定义属性）；
//   3. 建 Error 实例（stack 首行 "AggregateError: msg"）；
//   4. 原型改挂 AggregateError.prototype；
//   5. 定义自有 errors 属性 = 收集到的数组（writable/不可枚举/可配置）。
// 注意 [[AggregateErrors]] 内部槽在规范中没有读取操作，只体现为 errors 属性，
// 故不新增 Data 字段（ADR-040）。
Value* aggregateErrorCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    // errors 缺失时 ToList(undefined) 会因不可迭代抛 TypeError（§20.5.7.1 step 2）
    Value* errorsArg = argc > 0 ? argv[0] : ctx->createUndefined();
    std::vector<Value*> items;
    if (Value* err = detail::iterableToList(ctx, errorsArg, items)) return err;

    std::string message;
    if (argc > 1) {
        Value* err = nullptr;
        size_t msgArgc = 1;
        Value* msgArgv[1] = {argv[1]};
        messageArgument(ctx, msgArgc, msgArgv, message, err);
        if (err != nullptr) return err;
    }

    Value* instance = ctx->createError(message, "AggregateError");
    if (instance == ctx->oomValue()) return instance;

    Value* ctor = ctx->global()->lookup("AggregateError");
    if (ctor == nullptr) {
        return detail::typeError(ctx, "internal: missing error constructor");
    }
    Value* proto = ctx->getProperty(ctor, "prototype");
    if (detail::isErrorValue(proto)) return proto;
    Value* r = ctx->setPrototype(instance, proto);
    if (detail::isErrorValue(r)) return r;

    // errors 属性：把收集到的元素装成数组（§20.5.7.1 step 5）
    Value* errors = ctx->createArray();
    if (errors == ctx->oomValue()) return errors;
    auto* d = static_cast<ArrayData*>(errors->data());
    for (size_t i = 0; i < items.size(); ++i) {
        d->setElement(static_cast<uint32_t>(i), items[i]);
    }
    r = ctx->defineProperty(instance, "errors", errors, true, false, true);
    if (detail::isErrorValue(r)) return r;
    return instance;
}

Value* typeErrorCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    return nativeErrorCtor(ctx, "TypeError", "TypeError", argc, argv);
}
Value* rangeErrorCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    return nativeErrorCtor(ctx, "RangeError", "RangeError", argc, argv);
}
Value* referenceErrorCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    return nativeErrorCtor(ctx, "ReferenceError", "ReferenceError", argc, argv);
}
Value* syntaxErrorCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    return nativeErrorCtor(ctx, "SyntaxError", "SyntaxError", argc, argv);
}
Value* uriErrorCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    return nativeErrorCtor(ctx, "URIError", "URIError", argc, argv);
}

// Error.prototype.toString()（ES2024 §20.5.3.4）：
//   name 缺省 "Error"，message 缺省 ""；两者按 "name: message" 拼接，
//   message 为空时只返回 name。
Value* errorProtoToString(IContext* ctx, Value* self, size_t /*argc*/,
                          Value* /*argv*/[]) {
    Value* nameValue = ctx->getProperty(self, "name");
    if (detail::isErrorValue(nameValue)) return nameValue;
    Value* messageValue = ctx->getProperty(self, "message");
    if (detail::isErrorValue(messageValue)) return messageValue;

    std::string name = nameValue->type()->kind() == TypeKind::Undefined
                           ? "Error"
                           : detail::valueToString(nameValue);
    std::string message = messageValue->type()->kind() == TypeKind::Undefined
                              ? ""
                              : detail::valueToString(messageValue);
    if (name.empty()) return ctx->createString(message);
    if (message.empty()) return ctx->createString(name);
    return ctx->createString(name + ": " + message);
}

// 把值挂到 global scope：add 跟踪 + bind 名字
void bindGlobal(IContext* ctx, std::string_view name, Value* v) {
    ctx->global()->add(v);
    ctx->global()->bind(name, v);
}

// 注册一个 NativeError 子类（ES2024 §20.5.6.2/§20.5.6.3）：
//   1. 原型 = 普通对象，原型链 → %Error.prototype%
//   2. 构造函数（原型 = Function.prototype，prototype 属性 → 子类原型）
//   3. 双向 constructor
//   4. 子类原型自有 name（writable/不可枚举/可配置）
//   5. 注册到 global
bool registerNativeError(IContext* ctx, Value* errorProto, const char* name,
                         NativeFn ctor) {
    Value* proto = ctx->createObject();
    if (proto == ctx->oomValue()) return false;
    if (ctx->setPrototype(proto, errorProto)->type()->kind() == TypeKind::Exception) {
        return false;
    }

    Value* fn = ctx->createNativeFunction(ctor, 0);
    if (fn == ctx->oomValue()) return false;
    ctx->setFunctionName(fn, name);
    // fn.prototype → proto（不可写/不可枚举/不可配置）
    if (ctx->defineProperty(fn, "prototype", proto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // proto.constructor → fn
    if (ctx->defineProperty(proto, "constructor", fn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // proto.name = name（writable/不可枚举/可配置，ES2024 §20.5.6.3.1）
    if (ctx->defineProperty(proto, "name", ctx->createString(name), true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    bindGlobal(ctx, name, fn);
    return true;
}

// 定义 prototype 方法（writable/不可枚举/可配置，与 ES 一致）
bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

}  // namespace

bool registerErrorBuiltins(IContext* ctx) {
    // —— 1. Error.prototype（**普通对象**，原型 = Object.prototype）——
    // ES2024 §20.5.3：%Error.prototype% 是 ordinary object，**没有** [[ErrorData]]
    // 内部槽（各子类原型 §20.5.6.3 同理）。故用 createObject 而非 createError。
    Value* errorProto = ctx->createObject();
    if (errorProto == ctx->oomValue()) return false;
    if (ctx->setPrototype(errorProto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }

    // 原型就绪：后续 createError 自动挂链
    ctx->setErrorPrototype(errorProto);

    // —— 2. Error 构造函数 ——
    Value* errorFn = ctx->createNativeFunction(errorCtor, 0);
    if (errorFn == ctx->oomValue()) return false;
    ctx->setFunctionName(errorFn, "Error");
    // Error.prototype 属性指向 errorProto（不可写/不可枚举/不可配置，ES2024 §20.5.2.1）
    if (ctx->defineProperty(errorFn, "prototype", errorProto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // errorProto.constructor = Error
    if (ctx->defineProperty(errorProto, "constructor", errorFn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— 3. Error.prototype.name / message / toString ——
    // name：writable / 不可枚举 / 可配置（ES2024 §20.5.3.2）
    if (ctx->defineProperty(errorProto, "name", ctx->createString("Error"), true, false,
                            true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // message：writable / 不可枚举 / 可配置，初值 ""（ES2024 §20.5.3.1）
    if (ctx->defineProperty(errorProto, "message", ctx->createString(""), true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineMethod(ctx, errorProto, "toString", errorProtoToString, 0)) return false;

    // —— 4. 注册到 global ——
    bindGlobal(ctx, "Error", errorFn);

    // —— 5. 一般性错误子类（ES2024 §20.5.6）——
    // EvalError 已不在规范中，不实现；AggregateError / SuppressedError 不在本阶段范围。
    // 每个子类：原型是**普通对象**（无 [[ErrorData]]），原型链指向 %Error.prototype%，
    // 自有 name（writable/不可枚举/可配置）与 constructor。
    if (!registerNativeError(ctx, errorProto, "TypeError", typeErrorCtor)) return false;
    if (!registerNativeError(ctx, errorProto, "RangeError", rangeErrorCtor)) return false;
    if (!registerNativeError(ctx, errorProto, "ReferenceError", referenceErrorCtor))
        return false;
    if (!registerNativeError(ctx, errorProto, "SyntaxError", syntaxErrorCtor)) return false;
    if (!registerNativeError(ctx, errorProto, "URIError", uriErrorCtor)) return false;

    // —— 6. AggregateError（ES2024 §20.5.7）——
    // 与一般性错误子类同一注册路径（共用 TypeKind::Error，靠原型链区分）。
    if (!registerNativeError(ctx, errorProto, "AggregateError", aggregateErrorCtor))
        return false;
    return true;
}

}  // namespace neo::core
