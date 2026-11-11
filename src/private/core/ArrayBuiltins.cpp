// ArrayBuiltins.cpp - Array 构造函数与 Array.prototype 注册实现
//
// bootstrap 顺序（依赖 Object/Function 内建已就绪）：
//   1. Array.prototype（数组，原型 = Object.prototype）
//   2. setArrayPrototype —— 此后 createArray 自动挂链
//   3. Array 构造函数（替换自动生成的 prototype 属性为 Array.prototype）
//   4. 双向 constructor 引用
//   5. prototype 方法
//   6. 注册到 global
//
// 构造函数语义（ES2024 §23.1.1.1）：
//   Array()            → 空数组
//   new Array(n)       → 长度 n（n 为 0..2^32-1 的整数，否则 Exception）
//   new Array(a, b, c) → 元素数组
//
// 关联 ADR: 020（标准库组织）、018（对象模型 §6）、034（原型-构造函数）

#include "core/ArrayBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Number.h"
#include "core/types/Proxy.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <cmath>
#include <string>

namespace neo::core {

namespace {

// ToObject（ES2024 §7.1.18）：null/undefined 抛 TypeError，原始值装箱。
Value* toObject(IContext* ctx, Value* v, const char* method) {
    TypeKind k = v->type()->kind();
    if (k == TypeKind::Undefined || k == TypeKind::Null) {
        return detail::typeError(
            ctx, std::string("Array.prototype.") + method + " called on null or undefined");
    }
    if (detail::isObjectKind(k)) return v;
    return ctx->createPrimitiveWrapper(v);
}

// LengthOfArrayLike（ES2024 §7.3.20）：ToLength(Get(O, "length"))。
// 引擎数组长度上限为 2^32-1，超出按上限截断。
Value* lengthOfArrayLike(IContext* ctx, Value* obj, uint32_t& out) {
    Value* lenV = ctx->getProperty(obj, "length");
    if (detail::isErrorValue(lenV)) return lenV;
    Value* nv = detail::toNumberValue(ctx, lenV);
    if (detail::isErrorValue(nv)) return nv;
    double n = static_cast<NumberData*>(nv->data())->value;
    if (std::isnan(n) || n <= 0.0) {
        out = 0;
        return nullptr;
    }
    if (n >= static_cast<double>(kArrayMaxLength)) {
        out = kArrayMaxLength;
        return nullptr;
    }
    out = static_cast<uint32_t>(std::floor(n));
    return nullptr;
}

Value* indexKey(IContext* ctx, uint32_t index) {
    return ctx->createString(std::to_string(index));
}

// DeletePropertyOrThrow：[[Delete]] 返回 false 时抛 TypeError。
Value* deletePropertyOrThrow(IContext* ctx, Value* obj, Value* key) {
    Value* r = ctx->deleteProperty(obj, key);
    if (detail::isErrorValue(r)) return r;
    if (!detail::toBoolean(r)) {
        return detail::typeError(ctx, "Cannot delete property");
    }
    return nullptr;
}

// ToIntegerOrInfinity（ES2024 §7.1.5），用于 slice/indexOf 的索引参数。
// 走完整 ToNumber（对象参数先 ToPrimitive），失败时写入 err 并返回 0。
double toIntegerOrInfinity(IContext* ctx, Value* v, Value*& err) {
    Value* nv = detail::toNumberValue(ctx, v);
    if (detail::isErrorValue(nv)) {
        err = nv;
        return 0.0;
    }
    double n = static_cast<NumberData*>(nv->data())->value;
    if (std::isnan(n) || n == 0.0) return 0.0;
    if (std::isinf(n)) return n;
    return std::trunc(n);
}

// 把相对索引（可负）归一化到 [0, len]
uint32_t clampIndex(double idx, uint32_t len) {
    if (idx < 0.0) {
        double fromEnd = static_cast<double>(len) + idx;
        return fromEnd <= 0.0 ? 0u : static_cast<uint32_t>(fromEnd);
    }
    if (idx >= static_cast<double>(len)) return len;
    return static_cast<uint32_t>(idx);
}

// —— 构造函数 ——

Value* arrayCtor(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createArray();
    // 单参数且为 number → 长度
    if (argc == 1 && argv[0]->type()->kind() == TypeKind::Number) {
        double n = static_cast<NumberData*>(argv[0]->data())->value;
        if (!std::isfinite(n) || n < 0.0 || n > static_cast<double>(kArrayMaxLength) ||
            std::floor(n) != n) {
            return detail::rangeError(ctx, "Invalid array length");
        }
        Value* arr = ctx->createArray();
        if (arr == ctx->oomValue()) return arr;
        auto* d = static_cast<ArrayData*>(arr->data());
        d->setLength(static_cast<uint32_t>(n));
        return arr;
    }
    // 其余：元素数组
    Value* arr = ctx->createArray();
    if (arr == ctx->oomValue()) return arr;
    auto* d = static_cast<ArrayData*>(arr->data());
    for (size_t i = 0; i < argc; ++i) {
        d->setElement(static_cast<uint32_t>(i), argv[i]);
    }
    return arr;
}

// Array.isArray(v)
Value* arrayIsArray(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createBoolean(false);
    // IsArray（ES2024 §7.2.2）：Proxy 递归检查 target。
    Value* v = argv[0];
    while (v != nullptr && v->type()->kind() == TypeKind::Proxy) {
        v = proxyTarget(v);
    }
    return ctx->createBoolean(v != nullptr && v->type()->kind() == TypeKind::Array);
}

// —— prototype 方法 ——

// push(...items)：追加元素，返回新长度（ES2024 §23.1.3.23）
Value* arrayPush(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "push");
    if (detail::isErrorValue(obj)) return obj;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    for (size_t i = 0; i < argc; ++i) {
        Value* r = ctx->setProperty(obj, indexKey(ctx, len + static_cast<uint32_t>(i)),
                                    argv[i]);
        if (detail::isErrorValue(r)) return r;
    }
    double newLen = static_cast<double>(len) + static_cast<double>(argc);
    Value* r = ctx->setProperty(obj, "length", ctx->createNumber(newLen));
    if (detail::isErrorValue(r)) return r;
    return ctx->createNumber(newLen);
}

// pop()：移除并返回最后一个元素；空数组返回 undefined（ES2024 §23.1.3.20）
Value* arrayPop(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* obj = toObject(ctx, self, "pop");
    if (detail::isErrorValue(obj)) return obj;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    if (len == 0) {
        Value* r = ctx->setProperty(obj, "length", ctx->createNumber(0));
        return detail::isErrorValue(r) ? r : ctx->createUndefined();
    }
    Value* key = indexKey(ctx, len - 1);
    Value* value = ctx->getProperty(obj, key);
    if (detail::isErrorValue(value)) return value;
    if (Value* err = deletePropertyOrThrow(ctx, obj, key)) return err;
    Value* r = ctx->setProperty(obj, "length", ctx->createNumber(len - 1));
    if (detail::isErrorValue(r)) return r;
    return value;
}

// join(separator)：默认 ","；空洞/null/undefined 元素按空串。
// 分隔符与元素都走 ToString（ES2024 §23.1.3.15），可触发用户 toString。
Value* arrayJoin(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "join");
    if (detail::isErrorValue(obj)) return obj;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    std::string sep = ",";
    if (argc > 0 && argv[0]->type()->kind() != TypeKind::Undefined) {
        Value* s = detail::toStringValue(ctx, argv[0]);
        if (detail::isErrorValue(s)) return s;
        sep = detail::valueToString(s);
    }
    std::string result;
    for (uint32_t i = 0; i < len; ++i) {
        if (i > 0) result += sep;
        Value* v = ctx->getProperty(obj, indexKey(ctx, i));
        if (detail::isErrorValue(v)) return v;
        TypeKind k = v->type()->kind();
        if (k == TypeKind::Undefined || k == TypeKind::Null) continue;
        Value* s = detail::toStringValue(ctx, v);
        if (detail::isErrorValue(s)) return s;
        result += detail::valueToString(s);
    }
    return ctx->createString(result);
}

// toString()：Get(O, "join") 可调用则以 O 为 this 调用（ES2024 §23.1.3.36）
Value* arrayToStringFn(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* obj = toObject(ctx, self, "toString");
    if (detail::isErrorValue(obj)) return obj;
    Value* join = ctx->getProperty(obj, "join");
    if (detail::isErrorValue(join)) return join;
    if (detail::isCallable(join)) return ctx->call(join, obj, 0, nullptr);
    Value* objectToString = ctx->getProperty(ctx->objectPrototype(), "toString");
    if (detail::isErrorValue(objectToString)) return objectToString;
    return ctx->call(objectToString, obj, 0, nullptr);
}

// slice(start, end)：浅拷贝区间到新数组（保留空洞，ES2024 §23.1.3.25）
Value* arraySlice(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "slice");
    if (detail::isErrorValue(obj)) return obj;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    Value* err = nullptr;
    uint32_t start = 0;
    if (argc > 0) {
        double raw = toIntegerOrInfinity(ctx, argv[0], err);
        if (err != nullptr) return err;
        start = clampIndex(raw, len);
    }
    uint32_t end = len;
    if (argc > 1 && argv[1]->type()->kind() != TypeKind::Undefined) {
        double raw = toIntegerOrInfinity(ctx, argv[1], err);
        if (err != nullptr) return err;
        end = clampIndex(raw, len);
    }
    Value* result = ctx->createArray();
    if (result == ctx->oomValue()) return result;
    for (uint32_t i = start; i < end; ++i) {
        Value* key = indexKey(ctx, i);
        Value* has = ctx->hasProperty(obj, key);
        if (detail::isErrorValue(has)) return has;
        if (!detail::toBoolean(has)) continue;
        Value* v = ctx->getProperty(obj, key);
        if (detail::isErrorValue(v)) return v;
        Value* r = ctx->defineProperty(result, indexKey(ctx, i - start), v, true, true, true);
        if (detail::isErrorValue(r)) return r;
    }
    return result;
}

// indexOf(searchElement, fromIndex)：严格相等，未找到返回 -1（ES2024 §23.1.3.13）
Value* arrayIndexOf(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "indexOf");
    if (detail::isErrorValue(obj)) return obj;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    if (argc == 0) return ctx->createNumber(-1.0);
    uint32_t from = 0;
    if (argc > 1) {
        Value* err = nullptr;
        double raw = toIntegerOrInfinity(ctx, argv[1], err);
        if (err != nullptr) return err;
        if (raw >= static_cast<double>(len)) return ctx->createNumber(-1.0);
        from = clampIndex(raw, len);
    }
    for (uint32_t i = from; i < len; ++i) {
        Value* v = ctx->getProperty(obj, indexKey(ctx, i));
        if (detail::isErrorValue(v)) return v;
        if (detail::strictEquals(v, argv[0])) {
            return ctx->createNumber(static_cast<double>(i));
        }
    }
    return ctx->createNumber(-1.0);
}

// 把值挂到 global scope：add 跟踪 + bind 名字
void bindGlobal(IContext* ctx, std::string_view name, Value* v) {
    ctx->global()->add(v);
    ctx->global()->bind(name, v);
}

// 定义 prototype 方法（writable/不可枚举/可配置，与 ES 一致）
bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc, bool strict = false) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    // 规范以 Throw=true 调用 [[Set]]/[[Delete]] 的方法需局部严格。
    if (strict) ctx->setFunctionStrict(f);
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

}  // namespace

bool registerArrayBuiltins(IContext* ctx) {
    // —— 1. Array.prototype（自身是数组，原型 = Object.prototype）——
    // 此时 arrayPrototype_ 尚未赋值，createArray 不会自动挂链。
    Value* arrayProto = ctx->createArray();
    if (arrayProto == ctx->oomValue()) return false;
    if (ctx->setPrototype(arrayProto, ctx->objectPrototype())->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }

    // 原型就绪：后续 createArray 自动挂链
    ctx->setArrayPrototype(arrayProto);

    // —— 2. Array 构造函数 ——
    Value* arrayFn = ctx->createNativeFunction(arrayCtor, 0);
    if (arrayFn == ctx->oomValue()) return false;
    ctx->setFunctionName(arrayFn, "Array");
    // Array.prototype 属性指向 arrayProto（不可写/不可枚举/不可配置，ES2024 §23.1.2.5）
    if (ctx->defineProperty(arrayFn, "prototype", arrayProto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // arrayProto.constructor = Array
    if (ctx->defineProperty(arrayProto, "constructor", arrayFn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— 3. Array.isArray ——
    Value* isArrayFn = ctx->createNativeFunction(arrayIsArray, 0);
    if (isArrayFn == ctx->oomValue()) return false;
    ctx->setFunctionName(isArrayFn, "isArray");
    if (ctx->defineProperty(arrayFn, "isArray", isArrayFn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— 4. Array.prototype 方法 ——
    if (!defineMethod(ctx, arrayProto, "push", arrayPush, 0, /*strict=*/true)) return false;
    if (!defineMethod(ctx, arrayProto, "pop", arrayPop, 0, /*strict=*/true)) return false;
    if (!defineMethod(ctx, arrayProto, "join", arrayJoin, 0)) return false;
    if (!defineMethod(ctx, arrayProto, "toString", arrayToStringFn, 0)) return false;
    if (!defineMethod(ctx, arrayProto, "slice", arraySlice, 0, /*strict=*/true)) return false;
    if (!defineMethod(ctx, arrayProto, "indexOf", arrayIndexOf, 0)) return false;

    // —— 5. 注册到 global ——
    bindGlobal(ctx, "Array", arrayFn);
    return true;
}

}  // namespace neo::core
