// ErrorBuiltins.cpp - Error 构造函数与 %Error.prototype% 注册实现
//
// bootstrap 顺序（依赖 Object/Function 内建已就绪）：
//   1. Error.prototype（Error 实例，原型 = Object.prototype）
//   2. setErrorPrototype —— 此后 createError 自动挂链
//   3. Error 构造函数（prototype 属性指向 Error.prototype，双向 constructor）
//   4. Error.prototype.name / message / toString
//   5. 注册到 global
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、034（原型-构造函数）、038（Error 基类）

#include "core/ErrorBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <string>

namespace neo::core {

namespace {

// Error(message) / new Error(message)（ES2024 §20.5.1.1）：
//   无参或 message 为 undefined → 不定义 message 属性；
//   否则 ToString(message) 后定义。call 与 new 语义相同。
Value* errorCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    std::string message;
    if (argc > 0 && argv[0]->type()->kind() != TypeKind::Undefined) {
        Value* s = detail::toStringValue(ctx, argv[0]);
        if (detail::isErrorValue(s)) return s;
        message = static_cast<StringData*>(s->data())->value;
    }
    return ctx->createError(message);
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
    // —— 1. Error.prototype（Error 实例，原型 = Object.prototype）——
    // 此时 errorPrototype_ 尚未赋值，createError 不会自动挂链。
    // 用 createError 而非 createObject：原型对象本身也是 [[ErrorData]] 对象
    // （ES2024 §20.5.3），且 stack 在原型上无意义（后续覆盖为 ""）。
    Value* errorProto = ctx->createError("");
    if (errorProto == ctx->oomValue()) return false;
    // 原型对象不应有自有 stack（ES2024 §20.5.3：原型只有 name/message/toString）
    if (ctx->deleteProperty(errorProto, "stack")->type()->kind() == TypeKind::Exception) {
        return false;
    }
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
    return true;
}

}  // namespace neo::core
