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
#include "unicode/Utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

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

// LengthOfArrayLike 的完整值（不截断到 2^32-1）：concat 需按 §23.1.3.2
// 检查 n + len > 2^53-1，截断会漏掉溢出并造成超长循环。
Value* lengthOfArrayLikeFull(IContext* ctx, Value* obj, double& out) {
    Value* lenV = ctx->getProperty(obj, "length");
    if (detail::isErrorValue(lenV)) return lenV;
    Value* nv = detail::toNumberValue(ctx, lenV);
    if (detail::isErrorValue(nv)) return nv;
    double n = static_cast<NumberData*>(nv->data())->value;
    if (std::isnan(n) || n <= 0.0) {
        out = 0.0;
        return nullptr;
    }
    constexpr double kMaxSafeInteger = 9007199254740991.0;
    out = n > kMaxSafeInteger ? kMaxSafeInteger : std::floor(n);
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

// HasProperty 的布尔读取；失败返回错误值（定义见迭代方法区）。
Value* hasElement(IContext* ctx, Value* obj, Value* key, bool& out);

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
        // 已撤销的 Proxy：IsArray 抛 TypeError（§7.2.2）。
        if (proxyHandler(v) == nullptr) {
            return detail::typeError(ctx,
                                     "Cannot perform IsArray on a revoked proxy");
        }
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

// shift()：移除并返回首元素（ES2024 §23.1.3.29）。
Value* arrayShift(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* obj = toObject(ctx, self, "shift");
    if (detail::isErrorValue(obj)) return obj;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    if (len == 0) {
        Value* r = ctx->setProperty(obj, "length", ctx->createNumber(0));
        return detail::isErrorValue(r) ? r : ctx->createUndefined();
    }
    Value* first = ctx->getProperty(obj, indexKey(ctx, 0));
    if (detail::isErrorValue(first)) return first;
    for (uint32_t k = 1; k < len; ++k) {
        Value* fromKey = indexKey(ctx, k);
        Value* toKey = indexKey(ctx, k - 1);
        Value* has = ctx->hasProperty(obj, fromKey);
        if (detail::isErrorValue(has)) return has;
        if (detail::toBoolean(has)) {
            Value* v = ctx->getProperty(obj, fromKey);
            if (detail::isErrorValue(v)) return v;
            Value* r = ctx->setProperty(obj, toKey, v);
            if (detail::isErrorValue(r)) return r;
        } else {
            if (Value* err = deletePropertyOrThrow(ctx, obj, toKey)) return err;
        }
    }
    if (Value* err = deletePropertyOrThrow(ctx, obj, indexKey(ctx, len - 1))) return err;
    Value* r = ctx->setProperty(obj, "length", ctx->createNumber(len - 1));
    if (detail::isErrorValue(r)) return r;
    return first;
}

// unshift(...items)：前插并返回新长度（ES2024 §23.1.3.34）。
Value* arrayUnshift(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "unshift");
    if (detail::isErrorValue(obj)) return obj;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    uint32_t argCount = static_cast<uint32_t>(argc);
    if (argCount > 0) {
        for (uint32_t k = len; k > 0; --k) {
            Value* fromKey = indexKey(ctx, k - 1);
            Value* toKey = indexKey(ctx, k + argCount - 1);
            Value* has = ctx->hasProperty(obj, fromKey);
            if (detail::isErrorValue(has)) return has;
            if (detail::toBoolean(has)) {
                Value* v = ctx->getProperty(obj, fromKey);
                if (detail::isErrorValue(v)) return v;
                Value* r = ctx->setProperty(obj, toKey, v);
                if (detail::isErrorValue(r)) return r;
            } else {
                if (Value* err = deletePropertyOrThrow(ctx, obj, toKey)) return err;
            }
        }
        for (uint32_t j = 0; j < argCount; ++j) {
            Value* r = ctx->setProperty(obj, indexKey(ctx, j), argv[j]);
            if (detail::isErrorValue(r)) return r;
        }
    }
    double newLen = static_cast<double>(len) + static_cast<double>(argCount);
    Value* r = ctx->setProperty(obj, "length", ctx->createNumber(newLen));
    if (detail::isErrorValue(r)) return r;
    return ctx->createNumber(newLen);
}

// toLocaleString()：逐元素调用其 toLocaleString 后以 "," 连接（§23.1.3.32）。
Value* arrayToLocaleString(IContext* ctx, Value* self, size_t /*argc*/,
                           Value* /*argv*/[]) {
    Value* obj = toObject(ctx, self, "toLocaleString");
    if (detail::isErrorValue(obj)) return obj;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    std::string out;
    for (uint32_t i = 0; i < len; ++i) {
        if (i > 0) out += ",";
        Value* v = ctx->getProperty(obj, indexKey(ctx, i));
        if (detail::isErrorValue(v)) return v;
        TypeKind k = v->type()->kind();
        if (k == TypeKind::Undefined || k == TypeKind::Null) continue;
        Value* fn = ctx->getProperty(v, "toLocaleString");
        if (detail::isErrorValue(fn)) return fn;
        if (!detail::isCallable(fn)) {
            return detail::typeError(ctx, "toLocaleString is not a function");
        }
        Value* s = ctx->call(fn, v, 0, nullptr);
        if (detail::isErrorValue(s)) return s;
        Value* str = ctx->toString(s);
        if (detail::isErrorValue(str)) return str;
        out += static_cast<StringData*>(str->data())->value;
    }
    return ctx->createString(out);
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
        Value* key = indexKey(ctx, i);
        bool present = false;
        if (Value* e = hasElement(ctx, obj, key, present)) return e;
        if (!present) continue;
        Value* v = ctx->getProperty(obj, key);
        if (detail::isErrorValue(v)) return v;
        if (detail::strictEquals(v, argv[0])) {
            return ctx->createNumber(static_cast<double>(i));
        }
    }
    return ctx->createNumber(-1.0);
}

// —— 迭代/查找/变换方法（ES2024 §23.1.3）——

// IsArray（§7.2.2）：Proxy 递归穿透 target。
bool isArrayValue(Value* v) {
    while (v != nullptr && v->type()->kind() == TypeKind::Proxy) {
        v = proxyTarget(v);
    }
    return v != nullptr && v->type()->kind() == TypeKind::Array;
}

Value* requireCallable(IContext* ctx, Value* cb, const char* method) {
    if (detail::isCallable(cb)) return nullptr;
    return detail::typeError(ctx, std::string("Array.prototype.") + method +
                                      " callback is not a function");
}

Value* argOrUndefined(IContext* ctx, size_t argc, Value* argv[], size_t i) {
    return i < argc ? argv[i] : ctx->createUndefined();
}

// HasProperty 的布尔读取；失败返回错误值。
Value* hasElement(IContext* ctx, Value* obj, Value* key, bool& out) {
    Value* h = ctx->hasProperty(obj, key);
    if (detail::isErrorValue(h)) return h;
    out = detail::toBoolean(h);
    return nullptr;
}

// CreateDataProperty（§7.3.3）：writable/enumerable/configurable 全 true。
// 成功返回 nullptr，失败返回错误值（便于遍历回调直接透传）。
Value* createData(IContext* ctx, Value* obj, Value* key, Value* value) {
    // Context::defineProperty 成功时返回被定义的值（可能是 undefined），
    // 失败已转成 TypeError；这里只需区分错误与成功。
    Value* r = ctx->defineProperty(obj, key, value, true, true, true);
    return detail::isErrorValue(r) ? r : nullptr;
}

// ArraySpeciesCreate（§23.1.3.1）：子类构造器可覆盖返回类型。
Value* arraySpeciesCreate(IContext* ctx, Value* original, uint32_t length) {
    if (!isArrayValue(original)) return ctx->createArray();
    Value* ctor = ctx->getProperty(original, "constructor");
    if (detail::isErrorValue(ctor)) return ctor;
    if (detail::isObjectKind(ctor->type()->kind())) {
        Value* species =
            ctx->getProperty(ctor, ctx->getWellKnownSymbol(WellKnownSymbol::Species));
        if (detail::isErrorValue(species)) return species;
        TypeKind sk = species->type()->kind();
        ctor = (sk == TypeKind::Undefined || sk == TypeKind::Null)
                   ? ctx->createUndefined()
                   : species;
    }
    if (ctor->type()->kind() == TypeKind::Undefined) return ctx->createArray();
    if (!detail::isCallable(ctor)) {
        return detail::typeError(ctx, "Array species constructor is not a constructor");
    }
    Value* arrayCtor = ctx->global()->lookup("Array");
    if (ctor == arrayCtor) return ctx->createArray();
    Value* len = ctx->createNumber(static_cast<double>(length));
    return ctx->construct(ctor, 1, &len);
}

// 回调驱动的遍历：visitor 返回 nullptr 表示继续，返回其他值表示提前返回。
template <typename Visitor>
Value* iterateArrayLike(IContext* ctx, Value* obj, uint32_t len, bool skipHoles,
                        Visitor&& visitor) {
    // 仅当原型链上不可能有索引属性时，才用「自有元素」快路径。
    bool fastArray = obj->type()->kind() == TypeKind::Array;
    if (fastArray) {
        Value* proto = obj->type()->getPrototype(ctx, obj);
        if (detail::isErrorValue(proto)) return proto;
        if (proto != nullptr &&
            !(proto->type()->kind() == TypeKind::Array &&
              static_cast<ArrayData*>(proto->data())->elements.empty())) {
            fastArray = false;
        }
    }
    for (uint32_t k = 0; k < len; ++k) {
        Value* key = nullptr;
        if (skipHoles) {
            bool present = false;
            if (fastArray) {
                // 稀疏数组空洞直接查内部表，避免每轮分配索引字符串。
                present = static_cast<ArrayData*>(obj->data())->hasElement(k);
            } else {
                key = indexKey(ctx, k);
                if (Value* err = hasElement(ctx, obj, key, present)) return err;
            }
            if (!present) continue;
        }
        if (key == nullptr) key = indexKey(ctx, k);
        Value* value = ctx->getProperty(obj, key);
        if (detail::isErrorValue(value)) return value;
        Value* r = visitor(k, value);
        if (r != nullptr) return r;
    }
    return nullptr;
}

Value* arrayForEach(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "forEach");
    if (detail::isErrorValue(obj)) return obj;
    Value* cb = argOrUndefined(ctx, argc, argv, 0);
    if (Value* err = requireCallable(ctx, cb, "forEach")) return err;
    Value* thisArg = argOrUndefined(ctx, argc, argv, 1);
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    Value* err = iterateArrayLike(ctx, obj, len, true, [&](uint32_t k, Value* v) -> Value* {
        Value* callArgs[3] = {v, ctx->createNumber(static_cast<double>(k)), obj};
        Value* r = ctx->call(cb, thisArg, 3, callArgs);
        return detail::isErrorValue(r) ? r : nullptr;
    });
    return err != nullptr ? err : ctx->createUndefined();
}

Value* arrayMap(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "map");
    if (detail::isErrorValue(obj)) return obj;
    Value* cb = argOrUndefined(ctx, argc, argv, 0);
    if (Value* err = requireCallable(ctx, cb, "map")) return err;
    Value* thisArg = argOrUndefined(ctx, argc, argv, 1);
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    Value* result = arraySpeciesCreate(ctx, obj, len);
    if (detail::isErrorValue(result)) return result;
    Value* err = iterateArrayLike(ctx, obj, len, true, [&](uint32_t k, Value* v) -> Value* {
        Value* callArgs[3] = {v, ctx->createNumber(static_cast<double>(k)), obj};
        Value* mapped = ctx->call(cb, thisArg, 3, callArgs);
        if (detail::isErrorValue(mapped)) return mapped;
        return createData(ctx, result, indexKey(ctx, k), mapped);
    });
    return err != nullptr ? err : result;
}

Value* arrayFilter(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "filter");
    if (detail::isErrorValue(obj)) return obj;
    Value* cb = argOrUndefined(ctx, argc, argv, 0);
    if (Value* err = requireCallable(ctx, cb, "filter")) return err;
    Value* thisArg = argOrUndefined(ctx, argc, argv, 1);
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    Value* result = arraySpeciesCreate(ctx, obj, 0);
    if (detail::isErrorValue(result)) return result;
    uint32_t to = 0;
    Value* err = iterateArrayLike(ctx, obj, len, true, [&](uint32_t k, Value* v) -> Value* {
        Value* callArgs[3] = {v, ctx->createNumber(static_cast<double>(k)), obj};
        Value* selected = ctx->call(cb, thisArg, 3, callArgs);
        if (detail::isErrorValue(selected)) return selected;
        if (!detail::toBoolean(selected)) return nullptr;
        Value* r = createData(ctx, result, indexKey(ctx, to), v);
        if (r != nullptr) return r;
        ++to;
        return nullptr;
    });
    return err != nullptr ? err : result;
}

Value* arraySome(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "some");
    if (detail::isErrorValue(obj)) return obj;
    Value* cb = argOrUndefined(ctx, argc, argv, 0);
    if (Value* err = requireCallable(ctx, cb, "some")) return err;
    Value* thisArg = argOrUndefined(ctx, argc, argv, 1);
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    Value* err = iterateArrayLike(ctx, obj, len, true, [&](uint32_t k, Value* v) -> Value* {
        Value* callArgs[3] = {v, ctx->createNumber(static_cast<double>(k)), obj};
        Value* r = ctx->call(cb, thisArg, 3, callArgs);
        if (detail::isErrorValue(r)) return r;
        return detail::toBoolean(r) ? ctx->createBoolean(true) : nullptr;
    });
    if (err != nullptr) return err;
    return ctx->createBoolean(false);
}

Value* arrayEvery(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "every");
    if (detail::isErrorValue(obj)) return obj;
    Value* cb = argOrUndefined(ctx, argc, argv, 0);
    if (Value* err = requireCallable(ctx, cb, "every")) return err;
    Value* thisArg = argOrUndefined(ctx, argc, argv, 1);
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    Value* err = iterateArrayLike(ctx, obj, len, true, [&](uint32_t k, Value* v) -> Value* {
        Value* callArgs[3] = {v, ctx->createNumber(static_cast<double>(k)), obj};
        Value* r = ctx->call(cb, thisArg, 3, callArgs);
        if (detail::isErrorValue(r)) return r;
        return detail::toBoolean(r) ? nullptr : ctx->createBoolean(false);
    });
    if (err != nullptr) return err;
    return ctx->createBoolean(true);
}

Value* arrayFindImpl(IContext* ctx, Value* self, size_t argc, Value* argv[],
                     bool fromEnd, bool returnIndex) {
    const char* method = fromEnd ? (returnIndex ? "findLastIndex" : "findLast")
                                 : (returnIndex ? "findIndex" : "find");
    Value* obj = toObject(ctx, self, method);
    if (detail::isErrorValue(obj)) return obj;
    Value* cb = argOrUndefined(ctx, argc, argv, 0);
    if (Value* err = requireCallable(ctx, cb, method)) return err;
    Value* thisArg = argOrUndefined(ctx, argc, argv, 1);
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    if (len == 0) return returnIndex ? ctx->createNumber(-1.0) : ctx->createUndefined();
    for (uint32_t step = 0; step < len; ++step) {
        uint32_t k = fromEnd ? len - 1 - step : step;
        Value* v = ctx->getProperty(obj, indexKey(ctx, k));
        if (detail::isErrorValue(v)) return v;
        Value* callArgs[3] = {v, ctx->createNumber(static_cast<double>(k)), obj};
        Value* r = ctx->call(cb, thisArg, 3, callArgs);
        if (detail::isErrorValue(r)) return r;
        if (!detail::toBoolean(r)) continue;
        return returnIndex ? ctx->createNumber(static_cast<double>(k)) : v;
    }
    return returnIndex ? ctx->createNumber(-1.0) : ctx->createUndefined();
}

Value* arrayFind(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return arrayFindImpl(ctx, self, argc, argv, false, false);
}
Value* arrayFindIndex(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return arrayFindImpl(ctx, self, argc, argv, false, true);
}
Value* arrayFindLast(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return arrayFindImpl(ctx, self, argc, argv, true, false);
}
Value* arrayFindLastIndex(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return arrayFindImpl(ctx, self, argc, argv, true, true);
}

Value* arrayReduceImpl(IContext* ctx, Value* self, size_t argc, Value* argv[],
                       bool fromEnd) {
    Value* obj = toObject(ctx, self, fromEnd ? "reduceRight" : "reduce");
    if (detail::isErrorValue(obj)) return obj;
    Value* cb = argOrUndefined(ctx, argc, argv, 0);
    if (Value* err = requireCallable(ctx, cb, fromEnd ? "reduceRight" : "reduce")) {
        return err;
    }
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;

    uint32_t step = 0;
    Value* accumulator = nullptr;
    if (argc > 1) {
        accumulator = argv[1];
    } else {
        bool found = false;
        for (; step < len; ++step) {
            uint32_t k = fromEnd ? len - 1 - step : step;
            Value* key = indexKey(ctx, k);
            bool present = false;
            if (Value* err = hasElement(ctx, obj, key, present)) return err;
            if (!present) continue;
            accumulator = ctx->getProperty(obj, key);
            if (detail::isErrorValue(accumulator)) return accumulator;
            found = true;
            ++step;
            break;
        }
        if (!found) {
            return detail::typeError(ctx, "Reduce of empty array with no initial value");
        }
    }

    for (; step < len; ++step) {
        uint32_t k = fromEnd ? len - 1 - step : step;
        Value* key = indexKey(ctx, k);
        bool present = false;
        if (Value* err = hasElement(ctx, obj, key, present)) return err;
        if (!present) continue;
        Value* v = ctx->getProperty(obj, key);
        if (detail::isErrorValue(v)) return v;
        Value* callArgs[4] = {accumulator, v, ctx->createNumber(static_cast<double>(k)),
                              obj};
        accumulator = ctx->call(cb, ctx->createUndefined(), 4, callArgs);
        if (detail::isErrorValue(accumulator)) return accumulator;
    }
    return accumulator;
}

Value* arrayReduce(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return arrayReduceImpl(ctx, self, argc, argv, false);
}
Value* arrayReduceRight(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    return arrayReduceImpl(ctx, self, argc, argv, true);
}

// SameValueZero（§7.2.11）：=== 且 NaN 等于 NaN。
bool sameValueZero(IContext* ctx, Value* a, Value* b) {
    (void)ctx;
    if (a->type()->kind() == TypeKind::Number && b->type()->kind() == TypeKind::Number) {
        double x = static_cast<NumberData*>(a->data())->value;
        double y = static_cast<NumberData*>(b->data())->value;
        if (std::isnan(x) && std::isnan(y)) return true;
    }
    return detail::strictEquals(a, b);
}

Value* arrayIncludes(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "includes");
    if (detail::isErrorValue(obj)) return obj;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    if (len == 0) return ctx->createBoolean(false);
    Value* err = nullptr;
    double raw = argc > 1 ? toIntegerOrInfinity(ctx, argv[1], err) : 0.0;
    if (err != nullptr) return err;
    if (raw >= static_cast<double>(len)) return ctx->createBoolean(false);
    uint32_t from = clampIndex(raw, len);
    Value* target = argOrUndefined(ctx, argc, argv, 0);
    for (uint32_t k = from; k < len; ++k) {
        Value* v = ctx->getProperty(obj, indexKey(ctx, k));
        if (detail::isErrorValue(v)) return v;
        if (sameValueZero(ctx, v, target)) return ctx->createBoolean(true);
    }
    return ctx->createBoolean(false);
}

Value* arrayLastIndexOf(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "lastIndexOf");
    if (detail::isErrorValue(obj)) return obj;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    if (len == 0) return ctx->createNumber(-1.0);
    Value* err = nullptr;
    double raw = argc > 1 ? toIntegerOrInfinity(ctx, argv[1], err)
                          : static_cast<double>(len) - 1.0;
    if (err != nullptr) return err;
    double from = raw >= 0.0 ? std::min(raw, static_cast<double>(len) - 1.0)
                             : static_cast<double>(len) + raw;
    if (from < 0.0) return ctx->createNumber(-1.0);
    Value* target = argOrUndefined(ctx, argc, argv, 0);
    for (double d = from; d >= 0.0; d -= 1.0) {
        uint32_t k = static_cast<uint32_t>(d);
        Value* key = indexKey(ctx, k);
        bool present = false;
        if (Value* e = hasElement(ctx, obj, key, present)) return e;
        if (!present) continue;
        Value* v = ctx->getProperty(obj, key);
        if (detail::isErrorValue(v)) return v;
        if (detail::strictEquals(v, target)) return ctx->createNumber(static_cast<double>(k));
    }
    return ctx->createNumber(-1.0);
}

// concat(...items)：展开数组/@@isConcatSpreadable，保留空洞。
Value* arrayConcat(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "concat");
    if (detail::isErrorValue(obj)) return obj;
    Value* result = arraySpeciesCreate(ctx, obj, 0);
    if (detail::isErrorValue(result)) return result;
    uint32_t n = 0;

    auto appendSpreadable = [&](Value* item) -> Value* {
        double lenD = 0.0;
        if (Value* err = lengthOfArrayLikeFull(ctx, item, lenD)) return err;
        if (static_cast<double>(n) + lenD > 9007199254740991.0) {
            return detail::typeError(ctx, "Invalid array length");
        }
        uint32_t len = lenD > static_cast<double>(kArrayMaxLength)
                           ? kArrayMaxLength
                           : static_cast<uint32_t>(lenD);
        for (uint32_t k = 0; k < len; ++k) {
            Value* key = indexKey(ctx, k);
            bool present = false;
            if (Value* err = hasElement(ctx, item, key, present)) return err;
            if (!present) {
                ++n;
                continue;
            }
            Value* v = ctx->getProperty(item, key);
            if (detail::isErrorValue(v)) return v;
            if (Value* err = createData(ctx, result, indexKey(ctx, n), v)) return err;
            ++n;
        }
        return nullptr;
    };

    auto appendOne = [&](Value* item) -> Value* {
        if (isArrayValue(item)) return appendSpreadable(item);
        Value* spreadable =
            ctx->getProperty(item, ctx->getWellKnownSymbol(WellKnownSymbol::IsConcatSpreadable));
        if (detail::isErrorValue(spreadable)) return spreadable;
        if (spreadable->type()->kind() != TypeKind::Undefined &&
            detail::toBoolean(spreadable)) {
            return appendSpreadable(item);
        }
        if (Value* err = createData(ctx, result, indexKey(ctx, n), item)) return err;
        ++n;
        return nullptr;
    };

    if (Value* err = appendOne(obj)) return err;
    for (size_t i = 0; i < argc; ++i) {
        if (Value* err = appendOne(argv[i])) return err;
    }
    Value* r = ctx->setProperty(result, "length", ctx->createNumber(static_cast<double>(n)));
    return detail::isErrorValue(r) ? r : result;
}

// splice(start, deleteCount, ...items)（ES2024 §23.1.3.29）
Value* arraySplice(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "splice");
    if (detail::isErrorValue(obj)) return obj;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    Value* err = nullptr;
    double rawStart = argc > 0 ? toIntegerOrInfinity(ctx, argv[0], err) : 0.0;
    if (err != nullptr) return err;
    uint32_t start = clampIndex(rawStart, len);

    uint32_t deleteCount = 0;
    if (argc == 1) {
        deleteCount = len - start;
    } else if (argc >= 2) {
        double rawDelete = toIntegerOrInfinity(ctx, argv[1], err);
        if (err != nullptr) return err;
        if (rawDelete <= 0.0) {
            deleteCount = 0;
        } else if (rawDelete >= static_cast<double>(len - start)) {
            deleteCount = len - start;
        } else {
            deleteCount = static_cast<uint32_t>(rawDelete);
        }
    }
    uint32_t itemCount = argc > 2 ? static_cast<uint32_t>(argc - 2) : 0;

    Value* removed = arraySpeciesCreate(ctx, obj, deleteCount);
    if (detail::isErrorValue(removed)) return removed;
    for (uint32_t k = 0; k < deleteCount; ++k) {
        Value* key = indexKey(ctx, start + k);
        bool present = false;
        if (Value* e = hasElement(ctx, obj, key, present)) return e;
        if (!present) continue;
        Value* v = ctx->getProperty(obj, key);
        if (detail::isErrorValue(v)) return v;
        if (Value* e = createData(ctx, removed, indexKey(ctx, k), v)) return e;
    }
    Value* r = ctx->setProperty(removed, "length",
                                ctx->createNumber(static_cast<double>(deleteCount)));
    if (detail::isErrorValue(r)) return r;

    if (itemCount < deleteCount) {
        for (uint32_t k = start; k < len - deleteCount; ++k) {
            Value* from = indexKey(ctx, k + deleteCount);
            Value* to = indexKey(ctx, k + itemCount);
            bool present = false;
            if (Value* e = hasElement(ctx, obj, from, present)) return e;
            if (present) {
                Value* v = ctx->getProperty(obj, from);
                if (detail::isErrorValue(v)) return v;
                if (Value* e = createData(ctx, obj, to, v)) return e;
            } else {
                if (Value* e = deletePropertyOrThrow(ctx, obj, to)) return e;
            }
        }
        for (uint32_t k = len; k > len - deleteCount + itemCount; --k) {
            if (Value* e = deletePropertyOrThrow(ctx, obj, indexKey(ctx, k - 1))) return e;
        }
    } else if (itemCount > deleteCount) {
        for (uint32_t k = len - deleteCount; k > start; --k) {
            Value* from = indexKey(ctx, k + deleteCount - 1);
            Value* to = indexKey(ctx, k + itemCount - 1);
            bool present = false;
            if (Value* e = hasElement(ctx, obj, from, present)) return e;
            if (present) {
                Value* v = ctx->getProperty(obj, from);
                if (detail::isErrorValue(v)) return v;
                if (Value* e = createData(ctx, obj, to, v)) return e;
            } else {
                if (Value* e = deletePropertyOrThrow(ctx, obj, to)) return e;
            }
        }
    }
    for (uint32_t i = 0; i < itemCount; ++i) {
        if (Value* e = createData(ctx, obj, indexKey(ctx, start + i), argv[2 + i])) {
            return e;
        }
    }
    double newLen = static_cast<double>(len) - static_cast<double>(deleteCount) +
                    static_cast<double>(itemCount);
    r = ctx->setProperty(obj, "length", ctx->createNumber(newLen));
    return detail::isErrorValue(r) ? r : removed;
}

// reverse()：原地反转，保留空洞（ES2024 §23.1.3.24）
Value* arrayReverse(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* obj = toObject(ctx, self, "reverse");
    if (detail::isErrorValue(obj)) return obj;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    for (uint32_t lower = 0; lower < len / 2; ++lower) {
        uint32_t upper = len - lower - 1;
        Value* lowerKey = indexKey(ctx, lower);
        Value* upperKey = indexKey(ctx, upper);
        bool lowerPresent = false;
        bool upperPresent = false;
        if (Value* e = hasElement(ctx, obj, lowerKey, lowerPresent)) return e;
        if (Value* e = hasElement(ctx, obj, upperKey, upperPresent)) return e;
        Value* lowerValue = nullptr;
        Value* upperValue = nullptr;
        if (lowerPresent) {
            lowerValue = ctx->getProperty(obj, lowerKey);
            if (detail::isErrorValue(lowerValue)) return lowerValue;
        }
        if (upperPresent) {
            upperValue = ctx->getProperty(obj, upperKey);
            if (detail::isErrorValue(upperValue)) return upperValue;
        }
        if (upperPresent) {
            if (Value* e = createData(ctx, obj, lowerKey, upperValue)) return e;
        } else {
            if (Value* e = deletePropertyOrThrow(ctx, obj, lowerKey)) return e;
        }
        if (lowerPresent) {
            if (Value* e = createData(ctx, obj, upperKey, lowerValue)) return e;
        } else {
            if (Value* e = deletePropertyOrThrow(ctx, obj, upperKey)) return e;
        }
    }
    return obj;
}

// UTF-16 码元序列（默认 sort 比较单位）。
void appendUtf16(uint32_t cp, std::vector<uint16_t>& out) {
    if (cp <= 0xFFFF) {
        out.push_back(static_cast<uint16_t>(cp));
        return;
    }
    uint32_t v = cp - 0x10000;
    out.push_back(static_cast<uint16_t>(0xD800 + (v >> 10)));
    out.push_back(static_cast<uint16_t>(0xDC00 + (v & 0x3FF)));
}

std::vector<uint16_t> toUtf16(std::string_view s) {
    std::vector<uint16_t> out;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = i;
        uint32_t cp = 0;
        if (!unicode::decodeCodePoint(s, j, cp)) {
            out.push_back(static_cast<uint16_t>(static_cast<unsigned char>(s[i])));
            ++i;
            continue;
        }
        appendUtf16(cp, out);
        i = j;
    }
    return out;
}

// sort(comparefn)（ES2024 §23.1.3.30）：undefined 恒排末尾，空洞保持末尾。
Value* arraySort(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "sort");
    if (detail::isErrorValue(obj)) return obj;
    Value* comparator = nullptr;
    if (argc > 0 && argv[0]->type()->kind() != TypeKind::Undefined) {
        if (!detail::isCallable(argv[0])) {
            return detail::typeError(ctx, "The comparison function must be callable");
        }
        comparator = argv[0];
    }
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;

    std::vector<Value*> values;
    values.reserve(len);
    for (uint32_t k = 0; k < len; ++k) {
        Value* key = indexKey(ctx, k);
        bool present = false;
        if (Value* err = hasElement(ctx, obj, key, present)) return err;
        if (!present) continue;
        Value* v = ctx->getProperty(obj, key);
        if (detail::isErrorValue(v)) return v;
        values.push_back(v);
    }

    bool failed = false;
    Value* failure = nullptr;
    auto compare = [&](Value* x, Value* y) -> bool {
        if (failed) return false;
        bool xUndef = x->type()->kind() == TypeKind::Undefined;
        bool yUndef = y->type()->kind() == TypeKind::Undefined;
        if (xUndef || yUndef) {
            if (xUndef && yUndef) return false;
            return !xUndef;  // 非 undefined 排在 undefined 前
        }
        if (comparator != nullptr) {
            Value* args[2] = {x, y};
            Value* r = ctx->call(comparator, ctx->createUndefined(), 2, args);
            if (detail::isErrorValue(r)) {
                failed = true;
                failure = r;
                return false;
            }
            Value* n = detail::toNumberValue(ctx, r);
            if (detail::isErrorValue(n)) {
                failed = true;
                failure = n;
                return false;
            }
            double d = static_cast<NumberData*>(n->data())->value;
            if (std::isnan(d) || d == 0.0) return false;
            return d < 0.0;
        }
        Value* xs = detail::toStringValue(ctx, x);
        if (detail::isErrorValue(xs)) {
            failed = true;
            failure = xs;
            return false;
        }
        Value* ys = detail::toStringValue(ctx, y);
        if (detail::isErrorValue(ys)) {
            failed = true;
            failure = ys;
            return false;
        }
        return toUtf16(detail::valueToString(xs)) <
               toUtf16(detail::valueToString(ys));
    };
    std::stable_sort(values.begin(), values.end(), compare);
    if (failed) return failure;

    uint32_t k = 0;
    for (Value* v : values) {
        if (Value* err = createData(ctx, obj, indexKey(ctx, k), v)) return err;
        ++k;
    }
    for (; k < len; ++k) {
        if (Value* err = deletePropertyOrThrow(ctx, obj, indexKey(ctx, k))) return err;
    }
    return obj;
}

// flat(depth) / flatMap(callback)（ES2024 §23.1.3.11 / §23.1.3.12）
Value* flattenInto(IContext* ctx, Value* target, uint32_t& targetIndex, Value* source,
                   uint32_t sourceLen, double depth, uint32_t start) {
    for (uint32_t k = start; k < sourceLen; ++k) {
        Value* key = indexKey(ctx, k);
        bool present = false;
        if (Value* err = hasElement(ctx, source, key, present)) return err;
        if (!present) continue;
        Value* value = ctx->getProperty(source, key);
        if (detail::isErrorValue(value)) return value;
        if (depth > 0.0 && isArrayValue(value)) {
            uint32_t innerLen = 0;
            if (Value* err = lengthOfArrayLike(ctx, value, innerLen)) return err;
            if (Value* err = flattenInto(ctx, target, targetIndex, value, innerLen,
                                         depth - 1.0, 0)) {
                return err;
            }
        } else {
            if (Value* err = createData(ctx, target, indexKey(ctx, targetIndex), value)) {
                return err;
            }
            ++targetIndex;
        }
    }
    return nullptr;
}

Value* arrayFlat(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "flat");
    if (detail::isErrorValue(obj)) return obj;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    double depth = 1.0;
    if (argc > 0) {
        Value* err = nullptr;
        depth = toIntegerOrInfinity(ctx, argv[0], err);
        if (err != nullptr) return err;
        if (depth < 0.0) depth = 0.0;
    }
    Value* result = arraySpeciesCreate(ctx, obj, 0);
    if (detail::isErrorValue(result)) return result;
    uint32_t targetIndex = 0;
    if (Value* err = flattenInto(ctx, result, targetIndex, obj, len, depth, 0)) return err;
    return result;
}

Value* arrayFlatMap(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "flatMap");
    if (detail::isErrorValue(obj)) return obj;
    Value* cb = argOrUndefined(ctx, argc, argv, 0);
    if (Value* err = requireCallable(ctx, cb, "flatMap")) return err;
    Value* thisArg = argOrUndefined(ctx, argc, argv, 1);
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    Value* result = arraySpeciesCreate(ctx, obj, 0);
    if (detail::isErrorValue(result)) return result;
    uint32_t targetIndex = 0;
    for (uint32_t k = 0; k < len; ++k) {
        Value* key = indexKey(ctx, k);
        bool present = false;
        if (Value* err = hasElement(ctx, obj, key, present)) return err;
        if (!present) continue;
        Value* v = ctx->getProperty(obj, key);
        if (detail::isErrorValue(v)) return v;
        Value* callArgs[3] = {v, ctx->createNumber(static_cast<double>(k)), obj};
        Value* mapped = ctx->call(cb, thisArg, 3, callArgs);
        if (detail::isErrorValue(mapped)) return mapped;
        if (isArrayValue(mapped)) {
            uint32_t innerLen = 0;
            if (Value* err = lengthOfArrayLike(ctx, mapped, innerLen)) return err;
            if (Value* err = flattenInto(ctx, result, targetIndex, mapped, innerLen, 0.0,
                                         0)) {
                return err;
            }
        } else {
            if (Value* err = createData(ctx, result, indexKey(ctx, targetIndex), mapped)) {
                return err;
            }
            ++targetIndex;
        }
    }
    return result;
}

Value* arrayAt(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "at");
    if (detail::isErrorValue(obj)) return obj;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    Value* err = nullptr;
    double raw = argc > 0 ? toIntegerOrInfinity(ctx, argv[0], err) : 0.0;
    if (err != nullptr) return err;
    double idx = raw >= 0.0 ? raw : static_cast<double>(len) + raw;
    if (idx < 0.0 || idx >= static_cast<double>(len)) return ctx->createUndefined();
    return ctx->getProperty(obj, indexKey(ctx, static_cast<uint32_t>(idx)));
}

Value* arrayFill(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "fill");
    if (detail::isErrorValue(obj)) return obj;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    Value* value = argOrUndefined(ctx, argc, argv, 0);
    Value* err = nullptr;
    double rawStart = argc > 1 ? toIntegerOrInfinity(ctx, argv[1], err) : 0.0;
    if (err != nullptr) return err;
    uint32_t start = clampIndex(rawStart, len);
    uint32_t end = len;
    if (argc > 2) {
        double rawEnd = toIntegerOrInfinity(ctx, argv[2], err);
        if (err != nullptr) return err;
        end = clampIndex(rawEnd, len);
    }
    for (uint32_t k = start; k < end; ++k) {
        if (Value* e = createData(ctx, obj, indexKey(ctx, k), value)) return e;
    }
    return obj;
}

Value* arrayCopyWithin(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* obj = toObject(ctx, self, "copyWithin");
    if (detail::isErrorValue(obj)) return obj;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, obj, len)) return err;
    Value* err = nullptr;
    double rawTarget = argc > 0 ? toIntegerOrInfinity(ctx, argv[0], err) : 0.0;
    if (err != nullptr) return err;
    uint32_t to = clampIndex(rawTarget, len);
    double rawStart = argc > 1 ? toIntegerOrInfinity(ctx, argv[1], err) : 0.0;
    if (err != nullptr) return err;
    uint32_t from = clampIndex(rawStart, len);
    uint32_t final = len;
    if (argc > 2) {
        double rawEnd = toIntegerOrInfinity(ctx, argv[2], err);
        if (err != nullptr) return err;
        final = clampIndex(rawEnd, len);
    }
    if (final <= from) return obj;
    uint32_t count = std::min(final - from, len - to);
    if (count == 0) return obj;
    int direction = 1;
    if (from < to && to < from + count) {
        direction = -1;
        from = from + count - 1;
        to = to + count - 1;
    }
    for (uint32_t i = 0; i < count; ++i) {
        Value* fromKey = indexKey(ctx, from);
        Value* toKey = indexKey(ctx, to);
        bool present = false;
        if (Value* e = hasElement(ctx, obj, fromKey, present)) return e;
        if (present) {
            Value* v = ctx->getProperty(obj, fromKey);
            if (detail::isErrorValue(v)) return v;
            if (Value* e = createData(ctx, obj, toKey, v)) return e;
        } else {
            if (Value* e = deletePropertyOrThrow(ctx, obj, toKey)) return e;
        }
        if (direction == 1) {
            ++from;
            ++to;
        } else {
            --from;
            --to;
        }
    }
    return obj;
}

Value* arrayKeys(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* obj = toObject(ctx, self, "keys");
    if (detail::isErrorValue(obj)) return obj;
    return ctx->createArrayIterator(obj, ArrayIterationKind::Key);
}

Value* arrayValues(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* obj = toObject(ctx, self, "values");
    if (detail::isErrorValue(obj)) return obj;
    return ctx->createArrayIterator(obj, ArrayIterationKind::Value);
}

Value* arrayEntries(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    Value* obj = toObject(ctx, self, "entries");
    if (detail::isErrorValue(obj)) return obj;
    return ctx->createArrayIterator(obj, ArrayIterationKind::KeyAndValue);
}

// Array.from(items, mapfn, thisArg)（ES2024 §23.1.2.1）
Value* arrayFrom(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* items = argOrUndefined(ctx, argc, argv, 0);
    Value* mapfn = argOrUndefined(ctx, argc, argv, 1);
    bool mapping = mapfn->type()->kind() != TypeKind::Undefined;
    if (mapping && !detail::isCallable(mapfn)) {
        return detail::typeError(ctx, "Array.from map function is not callable");
    }
    Value* thisArg = argOrUndefined(ctx, argc, argv, 2);

    Value* arrayCtor = ctx->global()->lookup("Array");
    bool useDefault = !detail::isConstructor(self) || arrayCtor == nullptr ||
                      self->data() == arrayCtor->data();

    Value* iteratorMethod =
        ctx->getProperty(items, ctx->getWellKnownSymbol(WellKnownSymbol::Iterator));
    if (detail::isErrorValue(iteratorMethod)) return iteratorMethod;

    if (iteratorMethod->type()->kind() != TypeKind::Undefined &&
        iteratorMethod->type()->kind() != TypeKind::Null) {
        // 迭代器分支：Construct(C) 先于 GetIterator（§23.1.2.1），随后逐项
        // 惰性迭代（mapfn 抛错/定义属性失败都要 IteratorClose）。
        Value* result = useDefault ? ctx->createArray()
                                   : ctx->construct(self, 0, nullptr);
        if (detail::isErrorValue(result)) return result;
        Value* iterator = detail::getIterator(ctx, items);
        if (detail::isErrorValue(iterator)) return iterator;
        uint32_t k = 0;
        for (;;) {
            Value* nextResult = detail::iteratorNext(ctx, iterator);
            if (detail::isErrorValue(nextResult)) {
                return detail::iteratorClose(ctx, iterator, nextResult);
            }
            Value* doneErr = nullptr;
            bool done = detail::iteratorComplete(ctx, nextResult, doneErr);
            if (doneErr != nullptr) {
                return detail::iteratorClose(ctx, iterator, doneErr);
            }
            if (done) break;
            Value* value = detail::iteratorValue(ctx, nextResult);
            if (detail::isErrorValue(value)) {
                return detail::iteratorClose(ctx, iterator, value);
            }
            if (mapping) {
                Value* callArgs[2] = {
                    value, ctx->createNumber(static_cast<double>(k))};
                value = ctx->call(mapfn, thisArg, 2, callArgs);
                if (detail::isErrorValue(value)) {
                    return detail::iteratorClose(ctx, iterator, value);
                }
            }
            if (Value* err = createData(ctx, result, indexKey(ctx, k), value)) {
                return detail::iteratorClose(ctx, iterator, err);
            }
            ++k;
        }
        Value* lenRes = ctx->setProperty(
            result, "length", ctx->createNumber(static_cast<double>(k)));
        if (detail::isErrorValue(lenRes)) return lenRes;
        return result;
    }

    Value* arrayLike = toObject(ctx, items, "from");
    if (detail::isErrorValue(arrayLike)) return arrayLike;
    uint32_t len = 0;
    if (Value* err = lengthOfArrayLike(ctx, arrayLike, len)) return err;
    Value* lenValue = ctx->createNumber(static_cast<double>(len));
    Value* result = useDefault ? ctx->createArray()
                               : ctx->construct(self, 1, &lenValue);
    if (detail::isErrorValue(result)) return result;
    for (uint32_t k = 0; k < len; ++k) {
        Value* value = ctx->getProperty(arrayLike, indexKey(ctx, k));
        if (detail::isErrorValue(value)) return value;
        if (mapping) {
            Value* callArgs[2] = {value, ctx->createNumber(static_cast<double>(k))};
            value = ctx->call(mapfn, thisArg, 2, callArgs);
            if (detail::isErrorValue(value)) return value;
        }
        if (Value* err = createData(ctx, result, indexKey(ctx, k), value)) return err;
    }
    Value* lenRes = ctx->setProperty(result, "length",
                                     ctx->createNumber(static_cast<double>(len)));
    if (detail::isErrorValue(lenRes)) return lenRes;
    return result;
}

// Array.of(...items)（ES2024 §23.1.2.3）
Value* arrayOf(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    Value* arrayCtor = ctx->global()->lookup("Array");
    // IsConstructor(C) 为真时 Construct(C, «len»)，否则 ArrayCreate(len)。
    Value* lenValue = ctx->createNumber(static_cast<double>(argc));
    Value* result = (!detail::isConstructor(self) || arrayCtor == nullptr ||
                     self->data() == arrayCtor->data())
                        ? ctx->createArray()
                        : ctx->construct(self, 1, &lenValue);
    if (detail::isErrorValue(result)) return result;
    for (size_t i = 0; i < argc; ++i) {
        if (Value* err = createData(ctx, result,
                                    indexKey(ctx, static_cast<uint32_t>(i)), argv[i])) {
            return err;
        }
    }
    Value* lenRes = ctx->setProperty(result, "length",
                                     ctx->createNumber(static_cast<double>(argc)));
    if (detail::isErrorValue(lenRes)) return lenRes;
    return result;
}

Value* arraySpeciesGetter(IContext* /*ctx*/, Value* self, size_t /*argc*/,
                          Value* /*argv*/[]) {
    return self;
}

// 把值定义为 global object 属性（全局环境记录的对象记录）。
void bindGlobal(IContext* ctx, std::string_view name, Value* v) {
    ctx->defineGlobal(name, v, true, false, true);
}

// 定义 prototype 方法（writable/不可枚举/可配置，与 ES 一致）
bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc, bool strict = false, int lengthOverride = -1) {
    // 内建方法：不可构造、无自有 prototype。
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    Value* f = ctx->createNativeFunction(fn, minArgc, {}, opts);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    // 规范以 Throw=true 调用 [[Set]]/[[Delete]] 的方法需局部严格。
    if (strict) ctx->setFunctionStrict(f);
    if (lengthOverride >= 0) {
        Value* len = ctx->createNumber(static_cast<double>(lengthOverride));
        if (len == ctx->oomValue()) return false;
        if (ctx->defineProperty(f, "length", len, false, false, true)->type()->kind() ==
            TypeKind::Exception) {
            return false;
        }
    }
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

    // —— 3. Array 静态方法 ——
    if (!defineMethod(ctx, arrayFn, "isArray", arrayIsArray, 1)) return false;
    if (!defineMethod(ctx, arrayFn, "from", arrayFrom, 0, false, 1)) return false;
    if (!defineMethod(ctx, arrayFn, "of", arrayOf, 0, false, 0)) return false;
    // Array[@@species]：getter 返回 this（§23.1.2.5）。
    {
        Value* getter = ctx->createNativeFunction(arraySpeciesGetter, 0);
        if (getter == ctx->oomValue()) return false;
        ctx->setFunctionName(getter, "get [Symbol.species]");
        if (ctx->defineAccessor(arrayFn,
                                ctx->getWellKnownSymbol(WellKnownSymbol::Species),
                                getter, nullptr, false, true)
                ->type()->kind() == TypeKind::Exception) {
            return false;
        }
    }

    // —— 4. Array.prototype 方法 ——
    if (!defineMethod(ctx, arrayProto, "push", arrayPush, 0, /*strict=*/true, 1))
        return false;
    if (!defineMethod(ctx, arrayProto, "pop", arrayPop, 0, /*strict=*/true)) return false;
    if (!defineMethod(ctx, arrayProto, "shift", arrayShift, 0, /*strict=*/true))
        return false;
    if (!defineMethod(ctx, arrayProto, "unshift", arrayUnshift, 0, /*strict=*/true, 1))
        return false;
    if (!defineMethod(ctx, arrayProto, "toLocaleString", arrayToLocaleString, 0))
        return false;
    if (!defineMethod(ctx, arrayProto, "join", arrayJoin, 0, false, 1)) return false;
    if (!defineMethod(ctx, arrayProto, "toString", arrayToStringFn, 0)) return false;
    if (!defineMethod(ctx, arrayProto, "slice", arraySlice, 0, /*strict=*/true, 2))
        return false;
    if (!defineMethod(ctx, arrayProto, "indexOf", arrayIndexOf, 0, false, 1)) return false;
    if (!defineMethod(ctx, arrayProto, "lastIndexOf", arrayLastIndexOf, 0, false, 1))
        return false;
    if (!defineMethod(ctx, arrayProto, "includes", arrayIncludes, 0, false, 1)) return false;

    // 迭代/变换。
    if (!defineMethod(ctx, arrayProto, "forEach", arrayForEach, 0, false, 1)) return false;
    if (!defineMethod(ctx, arrayProto, "map", arrayMap, 0, false, 1)) return false;
    if (!defineMethod(ctx, arrayProto, "filter", arrayFilter, 0, false, 1)) return false;
    if (!defineMethod(ctx, arrayProto, "some", arraySome, 0, false, 1)) return false;
    if (!defineMethod(ctx, arrayProto, "every", arrayEvery, 0, false, 1)) return false;
    if (!defineMethod(ctx, arrayProto, "reduce", arrayReduce, 0, false, 1)) return false;
    if (!defineMethod(ctx, arrayProto, "reduceRight", arrayReduceRight, 0, false, 1))
        return false;
    if (!defineMethod(ctx, arrayProto, "find", arrayFind, 0, false, 1)) return false;
    if (!defineMethod(ctx, arrayProto, "findIndex", arrayFindIndex, 0, false, 1))
        return false;
    if (!defineMethod(ctx, arrayProto, "findLast", arrayFindLast, 0, false, 1))
        return false;
    if (!defineMethod(ctx, arrayProto, "findLastIndex", arrayFindLastIndex, 0, false, 1))
        return false;
    if (!defineMethod(ctx, arrayProto, "flatMap", arrayFlatMap, 0, false, 1)) return false;
    if (!defineMethod(ctx, arrayProto, "flat", arrayFlat, 0, false, 0)) return false;

    // 原地变换/拼接。
    if (!defineMethod(ctx, arrayProto, "concat", arrayConcat, 0, false, 1)) return false;
    if (!defineMethod(ctx, arrayProto, "splice", arraySplice, 0, /*strict=*/true, 2))
        return false;
    if (!defineMethod(ctx, arrayProto, "reverse", arrayReverse, 0, /*strict=*/true, 0))
        return false;
    if (!defineMethod(ctx, arrayProto, "sort", arraySort, 0, /*strict=*/true, 1))
        return false;
    if (!defineMethod(ctx, arrayProto, "fill", arrayFill, 0, /*strict=*/true, 1))
        return false;
    if (!defineMethod(ctx, arrayProto, "copyWithin", arrayCopyWithin, 0,
                      /*strict=*/true, 2)) {
        return false;
    }
    if (!defineMethod(ctx, arrayProto, "at", arrayAt, 0, false, 1)) return false;

    // 迭代器。
    if (!defineMethod(ctx, arrayProto, "keys", arrayKeys, 0, false, 0)) return false;
    if (!defineMethod(ctx, arrayProto, "values", arrayValues, 0, false, 0)) return false;
    if (!defineMethod(ctx, arrayProto, "entries", arrayEntries, 0, false, 0)) return false;
    {
        Value* valuesFn = ctx->getProperty(arrayProto, "values");
        if (detail::isErrorValue(valuesFn)) return false;
        if (ctx->defineProperty(arrayProto,
                                ctx->getWellKnownSymbol(WellKnownSymbol::Iterator),
                                valuesFn, true, false, true)
                ->type()->kind() == TypeKind::Exception) {
            return false;
        }
    }

    // —— 5. 注册到 global ——
    bindGlobal(ctx, "Array", arrayFn);
    return true;
}

}  // namespace neo::core
