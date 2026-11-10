// TypeBehavior.h (private) - Type 行为方法共享转换辅助
//
// 转换链（ES2024 §7.1）：
//   ToPrimitive / OrdinaryToPrimitive —— §7.1.1 / §7.1.1.1
//     @@toPrimitive（exotic）优先，否则按 hint 依次尝试 valueOf / toString。
//   ToNumber / ToString / ToPropertyKey —— §7.1.4 / §7.1.14 / §7.1.19
//     对象先 ToPrimitive 再递归；错误经 Value* 通道传播（异常即值）。
//   Add / 数值二元运算 —— §13.15.3（任一侧 ToPrimitive 为 string → 拼接）
//
// 另有纯原始值的快速辅助（toNumber/numberToString/valueToString），不触发用户代码，
// 供不涉及对象的内部路径使用。
//
// 关联 ADR: 016（Type 体系修订 §5/§6）、017（Exception/Fatal 透传）、006（异常即值）

#ifndef NEO_CORE_PRIVATE_TYPES_TYPE_BEHAVIOR_H
#define NEO_CORE_PRIVATE_TYPES_TYPE_BEHAVIOR_H

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Boolean.h"
#include "core/types/Function.h"
#include "core/types/Null.h"
#include "core/types/Number.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/String.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace neo::core::detail {

// —— StringToNumber（ES2024 §7.1.4.1）——
// trim 空白后，"" → +0，"Infinity"/"+Infinity"/"-Infinity" → ±Infinity，
// 其余走 strtod 解析（"42"→42, "3.14"→3.14, "1e309"→Infinity, "x"→NaN）。
// 注意 JS 只认精确大小写的 "Infinity"（"infinity"/"INF" → NaN），
// 而 strtod 大小写不敏感会解析 "inf"/"infinity" → inf，故需特判拦截。
inline double stringToNumber(const std::string& s) {
    // 跳过前导空白
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    if (i >= s.size()) return 0.0;  // 空串或全空白 → +0
    // 跳过尾部空白，得到有效内容 [i, j)
    size_t j = s.size();
    while (j > i && (s[j-1] == ' ' || s[j-1] == '\t' || s[j-1] == '\n' || s[j-1] == '\r')) --j;
    size_t len = j - i;
    // JS 特判：仅 "Infinity" / "+Infinity" / "-Infinity" 精确匹配 → ±Infinity
    // （strtod 大小写不敏感会误判 "infinity"/"INF"，须在 strtod 前拦截）
    if (len == 8 && s.compare(i, 8, "Infinity") == 0) {
        return std::numeric_limits<double>::infinity();
    }
    if (len == 9 && s[i] == '+' && s.compare(i+1, 8, "Infinity") == 0) {
        return std::numeric_limits<double>::infinity();
    }
    if (len == 9 && s[i] == '-' && s.compare(i+1, 8, "Infinity") == 0) {
        return -std::numeric_limits<double>::infinity();
    }
    // 其余走 strtod（"42"/"3.14"/"1e309"/"0x1F" 等）
    // 注：strtod 不认 "NaN" 字面量（C 标准未要求），未匹配时 end==start → NaN。
    //     但 strtod 大小写不敏感会解析 "inf"/"infinity"/"INF" → inf，
    //     合法的 "Infinity" 已在上方特判返回，此处 strtod 若得 inf 需区分：
    //     数字溢出（如 "1e309"）合法 → inf；字面量误判（如 "infinity"）→ NaN。
    //     区分依据：trim 后首字符是数字或符号后跟数字 → 合法溢出；否则 → 误判。
    const char* start = s.c_str() + i;
    char* end = nullptr;
    double result = std::strtod(start, &end);
    // 跳过尾部空白（strtod 保证 end 非 nullptr）
    while (*end != '\0' &&
           (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        ++end;
    }
    // 未完全消费 → NaN
    if (*end != '\0') return std::nan("");
    // strtod 得 inf：若首字符非数字/符号 → 字面量误判（"infinity"/"INF"）→ NaN；
    // 若是数字/符号开头 → 合法溢出（"1e309"）→ 保留 inf。
    if (std::isinf(result)) {
        char c = s[i];
        if (c == '+' || c == '-') c = s[i + 1];  // 跳过符号看实际首字符
        if (c < '0' || c > '9') return std::nan("");  // 非数字开头 → 误判
    }
    return result;
}

// —— ToBoolean 转换（ES2024 §7.1.2）——
// undefined/null → false；±0/NaN → false，其余 number → true；"" → false，其余 string → true；
// symbol → true；对象（含包装对象/数组/函数）→ true。
inline bool toBoolean(Value* v) {
    switch (v->type()->kind()) {
        case TypeKind::Undefined:
        case TypeKind::Null:
            return false;
        case TypeKind::Boolean:
            return static_cast<BooleanData*>(v->data())->value;
        case TypeKind::Number: {
            double n = static_cast<NumberData*>(v->data())->value;
            return n != 0.0 && !std::isnan(n);
        }
        case TypeKind::String:
            return !static_cast<StringData*>(v->data())->value.empty();
        default:
            return true;  // symbol / object / function / array / wrapper
    }
}

// —— ToPrimitive（ES2024 §7.1.1 / §7.1.1.1）——
// preferredType 缺省（Default）在 OrdinaryToPrimitive 中等价于 NUMBER，
// 仅 @@toPrimitive 的 hint 字符串区分 "default"。
enum class PreferredType { Default, Number, String };

// 是否为对象（可走 ToPrimitive/ToObject 的类型）。
// 判定下沉到 IType::hasObjectLayout()（各 Type 构造时声明），新增 exotic 对象类型
// 无需改这里。
inline bool isObjectKind(TypeKind k) {
    return kindHasObjectLayout(k);
}

// 结果是否为错误值（Exception/Fatal）
inline bool isErrorValue(Value* v) {
    TypeKind k = v->type()->kind();
    return k == TypeKind::Exception || k == TypeKind::Fatal;
}

// 是否为控制流信号值（Interrupt，ADR-046）。与 isErrorValue 互不包含：
// Interrupt 不是错误，不参与 try-catch；只在协程/VM 边界产生与消费。
inline bool isInterruptValue(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::Interrupt;
}

// —— 规范错误异常（ADR-039）——
// 引擎内部报错统一走这里：按 ES2024 规定的错误类型创建 Error 对象，
// 再包进 Exception 通道（ADR-006）。避免各处硬编码 ErrorKind。
inline Value* errorOf(IContext* ctx, ErrorKind kind, const std::string& msg) {
    return ctx->createErrorException(kind, msg);
}

// TypeError：类型不符/不可调用/不可构造/不可写等（ES2024 §7.2.7 等）
inline Value* typeError(IContext* ctx, const std::string& msg) {
    return errorOf(ctx, ErrorKind::TypeError, msg);
}

// RangeError：数值越界（如数组长度）
inline Value* rangeError(IContext* ctx, const std::string& msg) {
    return errorOf(ctx, ErrorKind::RangeError, msg);
}

// ReferenceError：引用未定义的标识符
inline Value* referenceError(IContext* ctx, const std::string& msg) {
    return errorOf(ctx, ErrorKind::ReferenceError, msg);
}

// 是否为可调用值（Function）
inline bool isCallable(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::Function;
}

// IsConstructor(argument)（ES2024 §7.2.4）：
//   仅 Function 类型可能可构造，由 FunctionData::constructible 显式标记；
//   绑定函数在创建时继承 target 的可构造性（§10.4.1.3）。
inline bool isConstructor(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::Function) return false;
    return static_cast<FunctionData*>(v->data())->constructible;
}

// OrdinaryToPrimitive(O, hint)（ES2024 §7.1.1.1）：
// hint=String → 依次尝试 toString、valueOf；hint=Number → 依次尝试 valueOf、toString。
// 方法可调用则调用，结果非对象即返回；全部失败返回 Exception（TypeError）。
// 方法调用抛出的 Exception/Fatal 直接透传。
inline Value* ordinaryToPrimitive(IContext* ctx, Value* obj, PreferredType hint) {
    const char* names[2];
    if (hint == PreferredType::String) {
        names[0] = "toString";
        names[1] = "valueOf";
    } else {
        names[0] = "valueOf";
        names[1] = "toString";
    }
    for (const char* name : names) {
        Value* method = ctx->getProperty(obj, name);
        if (isErrorValue(method)) return method;
        if (!isCallable(method)) continue;
        Value* result = ctx->call(method, obj, 0, nullptr);
        if (isErrorValue(result)) return result;
        if (!isObjectKind(result->type()->kind())) return result;
    }
    return typeError(ctx, "Cannot convert object to primitive value");
}

// ToPrimitive(input, preferredType)（ES2024 §7.1.1）：
//   非对象原样返回；
//   对象先查 @@toPrimitive（可调用则以其结果为准，结果仍为对象则报错）；
//   否则 OrdinaryToPrimitive（缺省 hint 按 Number）。
inline Value* toPrimitive(IContext* ctx, Value* input,
                          PreferredType preferred = PreferredType::Default) {
    if (!isObjectKind(input->type()->kind())) return input;

    Value* exotic = ctx->getProperty(input, ctx->getWellKnownSymbol(WellKnownSymbol::ToPrimitive));
    if (isErrorValue(exotic)) return exotic;
    if (exotic->type()->kind() != TypeKind::Undefined) {
        if (!isCallable(exotic)) {
            return typeError(ctx, "Symbol.toPrimitive is not a function");
        }
        const char* hint = preferred == PreferredType::String   ? "string"
                           : preferred == PreferredType::Number ? "number"
                                                                : "default";
        Value* argv[1] = {ctx->createString(hint)};
        Value* result = ctx->call(exotic, input, 1, argv);
        if (isErrorValue(result)) return result;
        if (isObjectKind(result->type()->kind())) {
            return typeError(ctx, "Cannot convert object to primitive value");
        }
        return result;
    }
    // 缺省 hint 等价于 NUMBER（ES2024 §7.1.1 step 1.c）
    return ordinaryToPrimitive(
        ctx, input, preferred == PreferredType::String ? PreferredType::String
                                                       : PreferredType::Number);
}

// —— ToNumber 转换（ES2024 §7.1.5）——
// undefined → NaN, null → +0, true → 1, false → 0, string → 解析。
// 仅处理原始值：Number 由 toNumberValue 直接返回，对象（Array/包装对象等）
// 已由 toNumberValue 走 ToPrimitive，不会到达这里。
inline double toNumber(Value* v) {
    switch (v->type()->kind()) {
        case TypeKind::Undefined: return std::nan("");
        case TypeKind::Null:      return 0.0;
        case TypeKind::Boolean:   return static_cast<BooleanData*>(v->data())->value ? 1.0 : 0.0;
        case TypeKind::String:    return stringToNumber(static_cast<StringData*>(v->data())->value);
        default: return std::nan("");  // Number 由调用方短路；Exception/Fatal 入口拦截
    }
}

// —— NumberToString（简化版，ES2024 §6.1.6.1.20 近似）——
// NaN → "NaN", +Infinity → "Infinity", -Infinity → "-Infinity", +0/-0 → "0"
// 整数 → 无小数点，小数 → 裁剪 trailing zeros
inline std::string numberToString(double n) {
    if (std::isnan(n)) return "NaN";
    if (std::isinf(n)) return n > 0 ? "Infinity" : "-Infinity";
    if (n == 0.0) return "0";  // +0 和 -0 都 → "0"

    // 整数：无小数点
    if (std::floor(n) == n && std::abs(n) < 1e16) {
        return std::to_string(static_cast<long long>(n));
    }

    // 小数：用 std::to_string 后裁剪 trailing zeros（std::to_string(double) 必含 '.'）
    std::string s = std::to_string(n);
    size_t last = s.find_last_not_of('0');
    if (s[last] == '.') {
        s.erase(last);       // "3.0" → "3"
    } else {
        s.erase(last + 1);   // "3.14000" → "3.14"
    }
    return s;
}

// —— ToString 转换（ES2024 §7.1.14，无对象 ToPrimitive 退化）——
// 按 type 分派，返回字符串内容（不创建 Value，由调用方包装）。
// 注：Array 分支供 arrayToString 处理嵌套数组元素（[1,[2,3]] → "1,2,3"），
// 其余对象类型由 toStringValue 先走 ToPrimitive，不会到达这里。
inline std::string valueToString(Value* v) {
    switch (v->type()->kind()) {
        case TypeKind::Undefined: return "undefined";
        case TypeKind::Null:      return "null";
        case TypeKind::Boolean:   return static_cast<BooleanData*>(v->data())->value ? "true" : "false";
        case TypeKind::Number:    return numberToString(static_cast<NumberData*>(v->data())->value);
        case TypeKind::String:    return static_cast<StringData*>(v->data())->value;
        case TypeKind::Array:     return arrayToString(static_cast<ArrayData*>(v->data()));
        default: return "";  // 包装对象走 ToPrimitive；Exception/Fatal 入口拦截
    }
}

// 把任意值渲染成人类可读描述（诊断 / 未处理拒绝上报用）。
// Error 对象走其 toString（"Name: message"），String 取内容，其余走 valueToString；
// 对象无字符串表示时退化为类型名（避免打印空串）。
inline std::string describeValue(IContext* ctx, Value* v) {
    if (v == nullptr) return "undefined";
    TypeKind k = v->type()->kind();
    if (k == TypeKind::Error) {
        Value* toString = ctx->getProperty(v, "toString");
        if (!isErrorValue(toString) && isCallable(toString)) {
            Value* s = ctx->call(toString, v, 0, nullptr);
            if (!isErrorValue(s) && s->type()->kind() == TypeKind::String) {
                return static_cast<StringData*>(s->data())->value;
            }
        }
    }
    if (k == TypeKind::String) return static_cast<StringData*>(v->data())->value;
    std::string s = valueToString(v);
    if (s.empty() && isObjectKind(k)) return std::string(v->type()->name());
    return s;
}

// —— 完整转换（返回 Value*，可携带 Exception/Fatal，ES2024 §7.1.4/§7.1.14/§7.1.19）——

// ToNumber(argument)（ES2024 §7.1.4）：
//   对象先 ToPrimitive(NUMBER)，再对原始值递归；Symbol 抛 TypeError。
inline Value* toNumberValue(IContext* ctx, Value* v) {
    TypeKind k = v->type()->kind();
    if (k == TypeKind::Number) return v;
    if (k == TypeKind::Symbol) {
        return typeError(ctx, "Cannot convert a Symbol value to a number");
    }
    if (isObjectKind(k)) {
        Value* prim = toPrimitive(ctx, v, PreferredType::Number);
        if (isErrorValue(prim)) return prim;
        return toNumberValue(ctx, prim);
    }
    return ctx->createNumber(toNumber(v));
}

// ToString(argument)（ES2024 §7.1.14）：
//   对象先 ToPrimitive(STRING)，再对原始值递归；Symbol 抛 TypeError。
inline Value* toStringValue(IContext* ctx, Value* v) {
    TypeKind k = v->type()->kind();
    if (k == TypeKind::String) return v;
    if (k == TypeKind::Symbol) {
        return typeError(ctx, "Cannot convert a Symbol value to a string");
    }
    if (isObjectKind(k)) {
        Value* prim = toPrimitive(ctx, v, PreferredType::String);
        if (isErrorValue(prim)) return prim;
        return toStringValue(ctx, prim);
    }
    return ctx->createString(valueToString(v));
}

// ToLength(argument)（ES2024 §7.1.20）：
//   ToIntegerOrInfinity 后截断到 [0, 2^53-1]，再转 uint32（数组/迭代器场景）。
// 成功返回 nullptr 并写入 out；失败返回 Exception/Fatal。
// 供 Array.prototype / 迭代器 / Array.from / TypedArray 等所有按 length 展开的路径复用。
inline Value* toLengthValue(IContext* ctx, Value* v, uint32_t& out) {
    Value* nv = toNumberValue(ctx, v);
    if (isErrorValue(nv)) return nv;
    double n = static_cast<NumberData*>(nv->data())->value;
    if (n != n || n <= 0.0) n = 0.0;                 // NaN / 负数 → 0
    if (n > 4294967295.0) n = 4294967295.0;          // 上限 2^32-1（数组长度上限）
    out = static_cast<uint32_t>(n);
    return nullptr;
}

// 读 obj.length 并 ToLength（对象 → length 属性的常见组合，ES2024 §7.1.20）。
// 成功返回 nullptr 并写入 out；失败返回 Exception/Fatal。
inline Value* lengthOf(IContext* ctx, Value* obj, uint32_t& out) {
    Value* lenValue = ctx->getProperty(obj, "length");
    if (isErrorValue(lenValue)) return lenValue;
    return toLengthValue(ctx, lenValue, out);
}

// ToPropertyKey(argument)（ES2024 §7.1.19）：
//   先 ToPrimitive(STRING)；结果为 Symbol 则原样返回，否则 ToString。
// 返回的 Value* 可能是 Symbol（调用方据此分流到 symbols 容器）。
inline Value* toPropertyKeyValue(IContext* ctx, Value* v) {
    Value* key = toPrimitive(ctx, v, PreferredType::String);
    if (isErrorValue(key)) return key;
    if (key->type()->kind() == TypeKind::Symbol) return key;
    return toStringValue(ctx, key);
}

// Add(lVal, rVal)（ES2024 §13.15.3）：
//   两侧 ToPrimitive（缺省 hint）；任一为 string → 字符串拼接，否则数值相加。
inline Value* addValues(IContext* ctx, Value* l, Value* r) {
    Value* lp = toPrimitive(ctx, l);
    if (isErrorValue(lp)) return lp;
    Value* rp = toPrimitive(ctx, r);
    if (isErrorValue(rp)) return rp;
    if (lp->type()->kind() == TypeKind::String || rp->type()->kind() == TypeKind::String) {
        // 拼接前两侧都要 ToString：Symbol 在此抛 TypeError（ES2024 §7.1.14）
        Value* ls = toStringValue(ctx, lp);
        if (isErrorValue(ls)) return ls;
        Value* rs = toStringValue(ctx, rp);
        if (isErrorValue(rs)) return rs;
        return ctx->createString(valueToString(ls) + valueToString(rs));
    }
    // 数值相加：两侧 ToNumber（Symbol 在此抛 TypeError，ES2024 §7.1.4）
    Value* ln = toNumberValue(ctx, lp);
    if (isErrorValue(ln)) return ln;
    Value* rn = toNumberValue(ctx, rp);
    if (isErrorValue(rn)) return rn;
    return ctx->createNumber(static_cast<NumberData*>(ln->data())->value +
                             static_cast<NumberData*>(rn->data())->value);
}

// 数值二元运算（- * /，ES2024 §13.15.4 等）：
//   两侧 ToPrimitive(NUMBER) 后 ToNumber，再按 op 计算。
// op 为 '-'、'*'、'/' 之一。
inline Value* numericOp(IContext* ctx, char op, Value* l, Value* r) {
    Value* lp = toPrimitive(ctx, l, PreferredType::Number);
    if (isErrorValue(lp)) return lp;
    Value* rp = toPrimitive(ctx, r, PreferredType::Number);
    if (isErrorValue(rp)) return rp;
    Value* ln = toNumberValue(ctx, lp);
    if (isErrorValue(ln)) return ln;
    Value* rn = toNumberValue(ctx, rp);
    if (isErrorValue(rn)) return rn;
    double a = static_cast<NumberData*>(ln->data())->value;
    double b = static_cast<NumberData*>(rn->data())->value;
    switch (op) {
        case '-': return ctx->createNumber(a - b);
        case '*': return ctx->createNumber(a * b);
        default:  return ctx->createNumber(a / b);
    }
}

// —— 迭代协议（ES2024 §7.4）——
// for-of / spread / 解构 / Array.from / Map/Set 构造 / yield* 全部走这三个原语。
// 本阶段只有数组迭代器，但接口按通用协议实现：任何带 @@iterator 的对象都可用。

// GetIterator(obj, sync)（ES2024 §7.4.2）：
//   取 obj[@@iterator] → 必须是函数 → 调用得到 iterator（须为对象）。
// 成功返回迭代器 Value；失败返回 Exception/Fatal。
inline Value* getIterator(IContext* ctx, Value* obj) {
    Value* method =
        ctx->getProperty(obj, ctx->getWellKnownSymbol(WellKnownSymbol::Iterator));
    if (isErrorValue(method)) return method;
    if (!isCallable(method)) {
        return typeError(ctx, "value is not iterable");
    }
    Value* iter = ctx->call(method, obj, 0, nullptr);
    if (isErrorValue(iter)) return iter;
    if (!isObjectKind(iter->type()->kind())) {
        return typeError(ctx, "iterator result is not an object");
    }
    return iter;
}

// IteratorNext(iterator, value)（ES2024 §7.4.3）：
//   调 iterator.next()，结果须为对象。成功返回结果对象；失败返回 Exception/Fatal。
inline Value* iteratorNext(IContext* ctx, Value* iterator) {
    Value* next = ctx->getProperty(iterator, "next");
    if (isErrorValue(next)) return next;
    if (!isCallable(next)) {
        return typeError(ctx, "iterator next is not a function");
    }
    Value* result = ctx->call(next, iterator, 0, nullptr);
    if (isErrorValue(result)) return result;
    if (!isObjectKind(result->type()->kind())) {
        return typeError(ctx, "iterator result is not an object");
    }
    return result;
}

// IteratorComplete(iterResult)（ES2024 §7.4.4）：ToBoolean(result.done)
inline bool iteratorComplete(IContext* ctx, Value* iterResult, Value*& err) {
    err = nullptr;
    Value* done = ctx->getProperty(iterResult, "done");
    if (isErrorValue(done)) {
        err = done;
        return true;
    }
    return toBoolean(done);
}

// IteratorValue(iterResult)（ES2024 §7.4.5）：result.value
inline Value* iteratorValue(IContext* ctx, Value* iterResult) {
    return ctx->getProperty(iterResult, "value");
}

// IteratorClose(iterator, completion)（ES2024 §7.4.6）：
//   调 iterator.return()（无 return 则忽略）；结果须为对象。
// completion 为已发生的异常时优先返回它（异常不被 return 覆盖）。
inline Value* iteratorClose(IContext* ctx, Value* iterator, Value* completion) {
    Value* ret = ctx->getProperty(iterator, "return");
    if (isErrorValue(ret)) return completion != nullptr ? completion : ret;
    if (ret->type()->kind() == TypeKind::Undefined || ret->type()->kind() == TypeKind::Null) {
        return completion;
    }
    if (!isCallable(ret)) {
        return completion != nullptr
                   ? completion
                   : typeError(ctx, "iterator return is not a function");
    }
    Value* r = ctx->call(ret, iterator, 0, nullptr);
    if (isErrorValue(r)) return completion != nullptr ? completion : r;
    if (!isObjectKind(r->type()->kind())) {
        return completion != nullptr
                   ? completion
                   : typeError(ctx, "iterator result is not an object");
    }
    return completion;
}

// IterableToList(items)（ES2024 §7.4.2.1）：
//   取 @@iterator 并逐个 next 收集 value，直到 done。
// 成功返回 nullptr 并把元素写入 out；失败返回 Exception/Fatal。
// 异常路径按 §7.4.7 调 IteratorClose（completion 优先于 return 的错误）。
inline Value* iterableToList(IContext* ctx, Value* items, std::vector<Value*>& out) {
    Value* iterator = getIterator(ctx, items);
    if (isErrorValue(iterator)) return iterator;
    for (;;) {
        Value* result = iteratorNext(ctx, iterator);
        if (isErrorValue(result)) return iteratorClose(ctx, iterator, result);
        Value* err = nullptr;
        bool done = iteratorComplete(ctx, result, err);
        if (err != nullptr) return iteratorClose(ctx, iterator, err);
        if (done) return nullptr;
        Value* value = iteratorValue(ctx, result);
        if (isErrorValue(value)) return iteratorClose(ctx, iterator, value);
        out.push_back(value);
    }
}

// —— instanceof（ES2024 §13.10.2 / §7.3.21）——

// OrdinaryHasInstance(C, O)（ES2024 §7.3.21）：
//   1. C 不可调用 → false
//   2. C 是绑定函数 → 以 [[BoundTargetFunction]] 递归
//   3. O 非对象 → false
//   4. P = C.prototype，非对象 → TypeError
//   5. 沿 O 的原型链找 P
// 成功返回 Boolean；失败返回 Exception/Fatal。
inline Value* ordinaryHasInstance(IContext* ctx, Value* ctor, Value* obj) {
    if (!isCallable(ctor)) return ctx->createBoolean(false);

    // 绑定函数：以被绑定目标递归（§7.3.21 step 2）
    auto* fd = static_cast<FunctionData*>(ctor->data());
    if (fd->boundTarget != nullptr) {
        return ordinaryHasInstance(ctx, fd->boundTarget, obj);
    }

    if (!isObjectKind(obj->type()->kind())) return ctx->createBoolean(false);

    Value* proto = ctx->getProperty(ctor, "prototype");
    if (isErrorValue(proto)) return proto;
    if (!isObjectKind(proto->type()->kind())) {
        return typeError(ctx, "Function has non-object prototype in instanceof check");
    }

    for (Value* cur = ctx->getPrototype(obj);
         cur != nullptr && isObjectKind(cur->type()->kind());
         cur = ctx->getPrototype(cur)) {
        if (isErrorValue(cur)) return cur;
        if (cur == proto) return ctx->createBoolean(true);
    }
    return ctx->createBoolean(false);
}

// InstanceofOperator(V, target)（ES2024 §13.10.2）：
//   1. target 非对象 → TypeError
//   2. 取 target[@@hasInstance]；存在则以其结果为准（ToBoolean）
//   3. 否则 target 须可调用 → OrdinaryHasInstance
// 成功返回 Boolean；失败返回 Exception/Fatal。
inline Value* instanceOf(IContext* ctx, Value* value, Value* target) {
    if (!isObjectKind(target->type()->kind())) {
        return typeError(ctx, "Right-hand side of 'instanceof' is not an object");
    }
    Value* handler =
        ctx->getProperty(target, ctx->getWellKnownSymbol(WellKnownSymbol::HasInstance));
    if (isErrorValue(handler)) return handler;
    if (handler->type()->kind() != TypeKind::Undefined &&
        handler->type()->kind() != TypeKind::Null) {
        if (!isCallable(handler)) {
            return typeError(ctx, "Symbol.hasInstance is not a function");
        }
        Value* r = ctx->call(handler, target, 1, &value);
        if (isErrorValue(r)) return r;
        return ctx->createBoolean(toBoolean(r));
    }
    if (!isCallable(target)) {
        return typeError(ctx, "Right-hand side of 'instanceof' is not callable");
    }
    return ordinaryHasInstance(ctx, target, value);
}

// —— 透传拦截（ADR-017 §4 Fatal 透传 + ADR-006 异常即值）——
// 行为方法入口检查右操作数 b：Fatal/Exception 时透传返回（左操作数 a 的分派已走对应 Type，
// 不会进入此函数）。Fatal 优先于 Exception。
inline Value* passthrough(Value* a, Value* b) {
    (void)a;  // a 非 Fatal/Exception（分派保证），仅查 b
    if (b->type()->kind() == TypeKind::Fatal)     return b;
    if (b->type()->kind() == TypeKind::Exception) return b;
    return nullptr;
}

// —— 非函数类型调用报错（ADR-022）——
// 非函数类型被调用时返回 Exception("<TypeName> is not a function")。
// 各基础类型的 call 统一走此辅助。self 是被调用的非函数 Value。
inline Value* notAFunction(IContext* ctx, Value* self) {
    std::string msg = std::string(self->type()->name()) + " is not a function";
    return typeError(ctx, msg);
}

// —— 非构造函数 new 报错（ADR-034）——
// 非函数类型被 new 时返回 Exception("<TypeName> is not a constructor")。
inline Value* notAConstructor(IContext* ctx, Value* self) {
    std::string msg = std::string(self->type()->name()) + " is not a constructor";
    return typeError(ctx, msg);
}

// —— 属性键转字符串（ToPropertyKey 的字符串部分，ES2024 §7.1.19）——
// 非 symbol 键转成字符串用于 properties 容器；symbol 由调用方分流到 symbols。
// Object/Function 作键需 ToPrimitive（暂未实现），返回空串（调用方自行处理）。
inline std::string keyToString(Value* key) {
    switch (key->type()->kind()) {
        case TypeKind::Undefined: return "undefined";
        case TypeKind::Null:      return "null";
        case TypeKind::Boolean:   return static_cast<BooleanData*>(key->data())->value ? "true" : "false";
        case TypeKind::Number:    return numberToString(static_cast<NumberData*>(key->data())->value);
        case TypeKind::String:    return static_cast<StringData*>(key->data())->value;
        case TypeKind::Array:     return arrayToString(static_cast<ArrayData*>(key->data()));
        // 包装对象 ToPropertyKey：拆 [[PrimitiveValue]] 后递归（ES2024 §7.1.19）
        case TypeKind::PrimitiveWrapper:
            return keyToString(static_cast<PrimitiveWrapperData*>(key->data())->primitiveValue);
        default: return {};  // Symbol 走 symbols 容器；Exception/Fatal/Object 不可达
    }
}

// 属性键 → 字符串（ToPropertyKey 的字符串部分，ES2024 §7.1.19）。
// 成功返回 nullptr 并写入 out；失败返回 Exception/Fatal（对象键的 ToPrimitive 抛错等）。
// 调用方负责先分流 symbol 键（symbol 不进 properties 容器）。
inline Value* propertyKeyString(IContext* ctx, Value* key, std::string& out) {
    Value* k = toPropertyKeyValue(ctx, key);
    if (isErrorValue(k)) return k;
    out = keyToString(k);
    return nullptr;
}

// —— 非对象类型属性操作（ADR-018）——
// 原始值的属性读取先 ToObject（ES2024 §7.1.18）——包装成临时对象后走对象查找，
// 使 "abc".length / (1).toString() 这类写法可用；写入/定义仍返回 Exception
// （严格模式下给原始值赋属性报错）。
// 注：Boolean/Number 的包装对象无自有属性，查找会落到各自 prototype。
inline Value* primitiveGetProperty(IContext* ctx, Value* self, Value* key) {
    // undefined/null 无法 ToObject（ES2024 §7.1.18 抛 TypeError）。
    // 属性读取按 JS 语义返回 undefined（`undefined.x` 在非严格求值下不崩，
    // 严格模式报错由 VM 阶段处理）。
    TypeKind k = self->type()->kind();
    if (k == TypeKind::Undefined || k == TypeKind::Null) {
        return ctx->createUndefined();
    }
    Value* wrapper = ctx->createPrimitiveWrapper(self);
    if (wrapper->type()->kind() == TypeKind::Exception) return wrapper;
    return ctx->getProperty(wrapper, key);
}

inline Value* primitiveSetProperty(IContext* ctx, Value* self, Value* key,
                                   Value* /*value*/) {
    std::string msg = "Cannot set property '" + keyToString(key) + "' of " +
                      std::string(self->type()->name());
    return typeError(ctx, msg);
}

// —— 非对象类型原型操作（ADR-018 §1）——
// 基础类型无原型：读取返回 undefined（与 getProperty 一致），设置返回 Exception。
inline Value* primitiveGetPrototype(IContext* ctx, Value* /*self*/) {
    return ctx->createUndefined();
}

inline Value* primitiveSetPrototype(IContext* ctx, Value* self, Value* /*proto*/) {
    std::string msg = "Cannot set prototype of " + std::string(self->type()->name());
    return typeError(ctx, msg);
}

}  // namespace neo::core::detail

#endif  // NEO_CORE_PRIVATE_TYPES_TYPE_BEHAVIOR_H
