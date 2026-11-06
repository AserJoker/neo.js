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
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <cmath>
#include <string>

namespace neo::core {

namespace {

ArrayData* asArray(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::Array
               ? static_cast<ArrayData*>(v->data())
               : nullptr;
}

// 非数组 this 的统一报错
Value* notArray(IContext* ctx, const char* method) {
    std::string msg = std::string("Array.prototype.") + method +
                      " called on non-array";
    return detail::typeError(ctx, msg);
}

// 严格相等（ES2024 §7.2.16 IsStrictlyEqual 的简化版）
bool strictEquals(Value* a, Value* b) {
    TypeKind ak = a->type()->kind();
    if (ak != b->type()->kind()) return false;
    switch (ak) {
        case TypeKind::Undefined:
        case TypeKind::Null:
            return true;
        case TypeKind::Boolean:
            return static_cast<BooleanData*>(a->data())->value ==
                   static_cast<BooleanData*>(b->data())->value;
        case TypeKind::Number: {
            double x = static_cast<NumberData*>(a->data())->value;
            double y = static_cast<NumberData*>(b->data())->value;
            return x == y;  // NaN != NaN，+0 == -0
        }
        case TypeKind::String:
            return static_cast<StringData*>(a->data())->value ==
                   static_cast<StringData*>(b->data())->value;
        default:
            // 对象/函数/数组/symbol：data 身份相同才相等（引用语义）
            return a->data() == b->data();
    }
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
    return ctx->createBoolean(argv[0]->type()->kind() == TypeKind::Array);
}

// —— prototype 方法 ——

// push(...items)：追加元素，返回新长度
Value* arrayPush(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    ArrayData* d = asArray(self);
    if (d == nullptr) return notArray(ctx, "push");
    uint32_t len = d->length();
    for (size_t i = 0; i < argc; ++i) {
        d->setElement(len + static_cast<uint32_t>(i), argv[i]);
    }
    return ctx->createNumber(static_cast<double>(d->length()));
}

// pop()：移除并返回最后一个元素；空数组返回 undefined
Value* arrayPop(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    ArrayData* d = asArray(self);
    if (d == nullptr) return notArray(ctx, "pop");
    uint32_t len = d->length();
    if (len == 0) return ctx->createUndefined();
    Value* last = d->elementAt(len - 1);
    d->setLength(len - 1);
    return last != nullptr ? last : ctx->createUndefined();
}

// join(separator)：默认 ","；空洞/null/undefined 元素按空串。
// 分隔符与元素都走 ToString（ES2024 §23.1.3.15），可触发用户 toString。
Value* arrayJoin(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    ArrayData* d = asArray(self);
    if (d == nullptr) return notArray(ctx, "join");
    std::string sep = ",";
    if (argc > 0 && argv[0]->type()->kind() != TypeKind::Undefined) {
        Value* s = detail::toStringValue(ctx, argv[0]);
        if (detail::isErrorValue(s)) return s;
        sep = detail::valueToString(s);
    }
    std::string result;
    uint32_t len = d->length();
    for (uint32_t i = 0; i < len; ++i) {
        if (i > 0) result += sep;
        Value* v = d->elementAt(i);
        if (v == nullptr) continue;
        TypeKind k = v->type()->kind();
        if (k == TypeKind::Undefined || k == TypeKind::Null) continue;
        Value* s = detail::toStringValue(ctx, v);
        if (detail::isErrorValue(s)) return s;
        result += detail::valueToString(s);
    }
    return ctx->createString(result);
}

// toString()：等价 join(",")
Value* arrayToStringFn(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    ArrayData* d = asArray(self);
    if (d == nullptr) return notArray(ctx, "toString");
    return ctx->createString(arrayToString(d));
}

// slice(start, end)：浅拷贝区间到新数组（保留空洞）
Value* arraySlice(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    ArrayData* d = asArray(self);
    if (d == nullptr) return notArray(ctx, "slice");
    uint32_t len = d->length();
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
    auto* rd = static_cast<ArrayData*>(result->data());
    if (end > start) {
        rd->setLength(end - start);
        for (uint32_t i = start; i < end; ++i) {
            Value* v = d->elementAt(i);
            if (v != nullptr) rd->setElement(i - start, v);
        }
    }
    return result;
}

// indexOf(searchElement, fromIndex)：严格相等，未找到返回 -1
Value* arrayIndexOf(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    ArrayData* d = asArray(self);
    if (d == nullptr) return notArray(ctx, "indexOf");
    uint32_t len = d->length();
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
        Value* v = d->elementAt(i);
        if (v != nullptr && strictEquals(v, argv[0])) {
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
                  size_t minArgc) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
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
    if (!defineMethod(ctx, arrayProto, "push", arrayPush, 0)) return false;
    if (!defineMethod(ctx, arrayProto, "pop", arrayPop, 0)) return false;
    if (!defineMethod(ctx, arrayProto, "join", arrayJoin, 0)) return false;
    if (!defineMethod(ctx, arrayProto, "toString", arrayToStringFn, 0)) return false;
    if (!defineMethod(ctx, arrayProto, "slice", arraySlice, 0)) return false;
    if (!defineMethod(ctx, arrayProto, "indexOf", arrayIndexOf, 0)) return false;

    // —— 5. 注册到 global ——
    bindGlobal(ctx, "Array", arrayFn);
    return true;
}

}  // namespace neo::core
