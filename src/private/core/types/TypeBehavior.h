// TypeBehavior.h (private) - Type 行为方法共享转换辅助
//
// 基础类型间转换的共享辅助函数（toNumber/numberToString/valueToString）+ 透传拦截。
// 被各 Type 子类的 .cpp include，避免重复实现。
//
// 本阶段不实现 ToPrimitive（ADR-030 阶段3）——基础类型间转换靠调换 + type 自己的简单转换。
//
// 关联 ADR: 016（Type 体系修订 §5/§6）、017（Exception/Fatal 透传）、006（异常即值）

#ifndef NEO_CORE_PRIVATE_TYPES_TYPE_BEHAVIOR_H
#define NEO_CORE_PRIVATE_TYPES_TYPE_BEHAVIOR_H

#include "core/IType.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Boolean.h"
#include "core/types/Null.h"
#include "core/types/Number.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/String.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

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

// —— ToNumber 转换（ES2024 §7.1.5）——
// undefined → NaN, null → +0, true → 1, false → 0, number → 自身, string → 解析,
// array → ToPrimitive（join(",")）后按字符串解析。
inline double toNumber(Value* v) {
    switch (v->type()->kind()) {
        case TypeKind::Undefined: return std::nan("");
        case TypeKind::Null:      return 0.0;
        case TypeKind::Boolean:   return static_cast<BooleanData*>(v->data())->value ? 1.0 : 0.0;
        case TypeKind::Number:    return static_cast<NumberData*>(v->data())->value;
        case TypeKind::String:    return stringToNumber(static_cast<StringData*>(v->data())->value);
        case TypeKind::Array:     return stringToNumber(arrayToString(static_cast<ArrayData*>(v->data())));
        // 包装对象 ToPrimitive：拆 [[PrimitiveValue]] 后递归（ES2024 §7.1.1）
        case TypeKind::PrimitiveWrapper:
            return toNumber(static_cast<PrimitiveWrapperData*>(v->data())->primitiveValue);
        default: return std::nan("");  // Exception/Fatal 入口拦截，不可达
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
// 按 type 分派，返回字符串内容（不创建 Value，由调用方包装）
inline std::string valueToString(Value* v) {
    switch (v->type()->kind()) {
        case TypeKind::Undefined: return "undefined";
        case TypeKind::Null:      return "null";
        case TypeKind::Boolean:   return static_cast<BooleanData*>(v->data())->value ? "true" : "false";
        case TypeKind::Number:    return numberToString(static_cast<NumberData*>(v->data())->value);
        case TypeKind::String:    return static_cast<StringData*>(v->data())->value;
        case TypeKind::Array:     return arrayToString(static_cast<ArrayData*>(v->data()));
        // 包装对象 ToPrimitive：拆 [[PrimitiveValue]] 后递归（ES2024 §7.1.1）
        case TypeKind::PrimitiveWrapper:
            return valueToString(static_cast<PrimitiveWrapperData*>(v->data())->primitiveValue);
        default: return "";  // Exception/Fatal 入口拦截，不可达
    }
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
    return ctx->createException(ctx->createString(msg));
}

// —— 非构造函数 new 报错（ADR-034）——
// 非函数类型被 new 时返回 Exception("<TypeName> is not a constructor")。
inline Value* notAConstructor(IContext* ctx, Value* self) {
    std::string msg = std::string(self->type()->name()) + " is not a constructor";
    return ctx->createException(ctx->createString(msg));
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
    return ctx->createException(ctx->createString(msg));
}

// —— 非对象类型原型操作（ADR-018 §1）——
// 基础类型无原型：读取返回 undefined（与 getProperty 一致），设置返回 Exception。
inline Value* primitiveGetPrototype(IContext* ctx, Value* /*self*/) {
    return ctx->createUndefined();
}

inline Value* primitiveSetPrototype(IContext* ctx, Value* self, Value* /*proto*/) {
    std::string msg = "Cannot set prototype of " + std::string(self->type()->name());
    return ctx->createException(ctx->createString(msg));
}

}  // namespace neo::core::detail

#endif  // NEO_CORE_PRIVATE_TYPES_TYPE_BEHAVIOR_H
