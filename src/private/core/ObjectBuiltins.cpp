// ObjectBuiltins.cpp - Object / Function 构造函数与内建原型（ADR-035）
//
// bootstrap 顺序（关键：原型先于构造函数建立，避免 createObject/createNativeFunction
// 自动挂链时拿到未初始化的原型）：
//   1. Object.prototype（裸对象，原型 null）
//   2. Function.prototype（函数对象，原型 = Object.prototype）
//   3. Object 构造函数（原型 = Function.prototype，prototype 属性 = Object.prototype）
//   4. Function 构造函数（原型 = Function.prototype，prototype 属性 = Function.prototype）
//   5. 挂 prototype 方法
//   6. 注册到 global
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、034（原型-构造函数）

#include "core/ObjectBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Object.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <string>

namespace neo::core {

namespace {

// —— Object.prototype 方法 ——

// Object.prototype.toString()：简化版，按类型返回 "[object Xxx]"
Value* objectProtoToString(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    if (self == nullptr) return ctx->createString("[object Undefined]");
    const char* tag = "Object";
    switch (self->type()->kind()) {
        case TypeKind::Undefined: tag = "Undefined"; break;
        case TypeKind::Null:      tag = "Null"; break;
        case TypeKind::Boolean:   tag = "Boolean"; break;
        case TypeKind::Number:    tag = "Number"; break;
        case TypeKind::String:    tag = "String"; break;
        case TypeKind::Symbol:    tag = "Symbol"; break;
        case TypeKind::Function:  tag = "Function"; break;
        case TypeKind::Object:    tag = "Object"; break;
        case TypeKind::Array:     tag = "Array"; break;
        // 包装对象按 [[PrimitiveValue]] 的类型给出 tag（ES2024 §20.1.3.6）
        case TypeKind::PrimitiveWrapper: {
            Value* p = wrapperPrimitiveValue(self);
            switch (p != nullptr ? p->type()->kind() : TypeKind::Undefined) {
                case TypeKind::Boolean: tag = "Boolean"; break;
                case TypeKind::Number:  tag = "Number"; break;
                case TypeKind::String:  tag = "String"; break;
                case TypeKind::Symbol:  tag = "Symbol"; break;
                default:                tag = "Object"; break;
            }
            break;
        }
        default:                  tag = "Object"; break;
    }
    return ctx->createString(std::string("[object ") + tag + "]");
}

// Object.prototype.valueOf()：返回 this 自身
Value* objectProtoValueOf(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return self != nullptr ? self : ctx->createUndefined();
}

// Object.prototype.hasOwnProperty(key)：只查自有属性（不含原型链）
Value* objectProtoHasOwnProperty(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    TypeKind sk = self != nullptr ? self->type()->kind() : TypeKind::Undefined;
    if (self == nullptr ||
        (sk != TypeKind::Object && sk != TypeKind::Function && sk != TypeKind::Array &&
         sk != TypeKind::PrimitiveWrapper)) {
        return ctx->createException(
            ctx->createString("Object.prototype.hasOwnProperty called on non-object"));
    }
    if (argc == 0) return ctx->createBoolean(false);
    Value* key = argv[0];
    auto* d = static_cast<ObjectData*>(self->data());
    if (key->type()->kind() == TypeKind::Symbol) {
        return ctx->createBoolean(d->symbols.count(key->data()) != 0);
    }
    // ToPropertyKey（ES2024 §7.1.19）：对象键先 ToPrimitive(STRING)
    std::string k;
    if (Value* err = detail::propertyKeyString(ctx, key, k)) return err;
    // 数组：索引元素与 length 也是自有属性
    if (sk == TypeKind::Array) {
        auto* ad = static_cast<ArrayData*>(self->data());
        if (k == "length") return ctx->createBoolean(true);
        uint32_t index = 0;
        if (parseArrayIndex(k, index)) {
            return ctx->createBoolean(ad->hasElement(index));
        }
    }
    return ctx->createBoolean(d->properties.count(k) != 0);
}

// —— Function.prototype 方法 ——

// Function.prototype.toString()：占位，返回 "[native code]"
Value* functionProtoToString(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                             Value* /*argv*/[]) {
    return ctx->createString("function () { [native code] }");
}

// Function.prototype.call(thisArg, ...args)
Value* functionProtoCall(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (self == nullptr || self->type()->kind() != TypeKind::Function) {
        return ctx->createException(ctx->createString("Function.prototype.call called on non-function"));
    }
    Value* thisArg = argc > 0 ? argv[0] : ctx->createUndefined();
    return ctx->call(self, thisArg, argc > 0 ? argc - 1 : 0, argc > 0 ? argv + 1 : nullptr);
}

// Function.prototype.apply(thisArg, argsArray)
// 本阶段不支持数组（Array 未实现），argsArray 为 null/undefined 时按无参处理。
Value* functionProtoApply(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    if (self == nullptr || self->type()->kind() != TypeKind::Function) {
        return ctx->createException(ctx->createString("Function.prototype.apply called on non-function"));
    }
    Value* thisArg = argc > 0 ? argv[0] : ctx->createUndefined();
    // argsArray 非 null/undefined 时需要 Array（未实现）→ not implemented
    if (argc > 1) {
        TypeKind k = argv[1]->type()->kind();
        if (k != TypeKind::Null && k != TypeKind::Undefined) {
            return ctx->createException(ctx->createString("Function.prototype.apply: arguments array is not implemented"));
        }
    }
    return ctx->call(self, thisArg, 0, nullptr);
}

// Function.prototype.bind(thisArg, ...args)：需要 FunctionData.self 字段，暂占位
Value* functionProtoBind(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                         Value* /*argv*/[]) {
    return ctx->createException(ctx->createString("Function.prototype.bind is not implemented"));
}

// —— Object / Function 构造函数 ——

// Object(value)（ES2024 §20.1.1.1）：
//   无参/undefined/null → 新建空对象；
//   已是对象（Object/Function/Array/PrimitiveWrapper）→ 原样返回；
//   原始值（Boolean/Number/String/Symbol）→ 包装对象。
Value* objectCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createObject();
    Value* v = argv[0];
    TypeKind k = v->type()->kind();
    if (k == TypeKind::Object || k == TypeKind::Function || k == TypeKind::Array ||
        k == TypeKind::PrimitiveWrapper) {
        return v;
    }
    if (k == TypeKind::Undefined || k == TypeKind::Null) return ctx->createObject();
    // 原始值 → 包装对象（Boolean/Number/String/Symbol）
    return ctx->createPrimitiveWrapper(v);
}

// Function(...)：new Function 暂不支持，调用/构造均返回 Exception
Value* functionCtor(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    return ctx->createException(ctx->createString("Function constructor is not implemented"));
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

bool registerObjectFunctionBuiltins(IContext* ctx) {
    // —— 1. Object.prototype（裸对象，原型 null）——
    // 此时 objectPrototype_ 尚未赋值，createObject 不会自动挂链
    Value* objectProto = ctx->createObject();
    if (objectProto == ctx->oomValue()) return false;

    // —— 2. Function.prototype（函数对象，原型 = Object.prototype）——
    // 用占位 native 创建，稍后其行为由实际调用方决定（通常不会被直接调用）
    Value* functionProto = ctx->createNativeFunction(functionCtor, 0);
    if (functionProto == ctx->oomValue()) return false;
    ctx->setFunctionName(functionProto, "");
    if (ctx->setPrototype(functionProto, objectProto)->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // 原型就绪：后续 createObject/createNativeFunction 自动挂链
    ctx->setBuiltinPrototypes(objectProto, functionProto);

    // —— 3. Object 构造函数 ——
    Value* objectFn = ctx->createNativeFunction(objectCtor, 0);
    if (objectFn == ctx->oomValue()) return false;
    ctx->setFunctionName(objectFn, "Object");
    // Object.prototype 属性指向 objectProto（不可写/不可枚举/不可配置，ES2024 §20.1.2.4）
    if (ctx->defineProperty(objectFn, "prototype", objectProto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // objectProto.constructor = Object
    if (ctx->defineProperty(objectProto, "constructor", objectFn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— 4. Function 构造函数 ——
    Value* functionFn = ctx->createNativeFunction(functionCtor, 0);
    if (functionFn == ctx->oomValue()) return false;
    ctx->setFunctionName(functionFn, "Function");
    // Function.prototype 属性指向 functionProto（不可写/不可枚举/不可配置）
    if (ctx->defineProperty(functionFn, "prototype", functionProto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // functionProto.constructor = Function
    if (ctx->defineProperty(functionProto, "constructor", functionFn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— 5. Object.prototype 方法 ——
    if (!defineMethod(ctx, objectProto, "toString", objectProtoToString, 0)) return false;
    if (!defineMethod(ctx, objectProto, "valueOf", objectProtoValueOf, 0)) return false;
    if (!defineMethod(ctx, objectProto, "hasOwnProperty", objectProtoHasOwnProperty, 1))
        return false;

    // —— 6. Function.prototype 方法 ——
    if (!defineMethod(ctx, functionProto, "toString", functionProtoToString, 0)) return false;
    // minArgc=0：无参调用合法（thisArg 按 undefined 处理，ES2024 §20.2.3.3）
    if (!defineMethod(ctx, functionProto, "call", functionProtoCall, 0)) return false;
    if (!defineMethod(ctx, functionProto, "apply", functionProtoApply, 0)) return false;
    if (!defineMethod(ctx, functionProto, "bind", functionProtoBind, 0)) return false;

    // —— 7. 注册到 global ——
    bindGlobal(ctx, "Object", objectFn);
    bindGlobal(ctx, "Function", functionFn);
    return true;
}

}  // namespace neo::core
