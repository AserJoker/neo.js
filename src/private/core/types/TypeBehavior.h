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
#include "core/types/Boolean.h"
#include "core/types/Null.h"
#include "core/types/Number.h"
#include "core/types/String.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

namespace neo::core::detail {

// —— ToNumber 转换（ES2024 §7.1.5）——
// undefined → NaN, null → +0, true → 1, false → 0, number → 自身, string → 解析
inline double toNumber(Value* v) {
    switch (v->type()->kind()) {
        case TypeKind::Undefined: return std::nan("");
        case TypeKind::Null:      return 0.0;
        case TypeKind::Boolean:   return static_cast<BooleanData*>(v->data())->value ? 1.0 : 0.0;
        case TypeKind::Number:    return static_cast<NumberData*>(v->data())->value;
        case TypeKind::String: {
            // JS 字符串转数字（ES2024 §7.1.4.1 StringToNumber）：
            //   trim 空白后，"" → +0，"Infinity"/"+Infinity"/"-Infinity" → ±Infinity，
            //   其余走 strtod 解析（"42"→42, "3.14"→3.14, "1e309"→Infinity, "x"→NaN）。
            //   注意 JS 只认精确大小写的 "Infinity"（"infinity"/"INF" → NaN），
            //   而 strtod 大小写不敏感会解析 "inf"/"infinity" → inf，故需特判拦截。
            const auto& s = static_cast<StringData*>(v->data())->value;
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
            // 跳过尾部空白
            while (end != nullptr && *end != '\0' &&
                   (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
                ++end;
            }
            // 未完全消费 → NaN
            if (end == nullptr || *end != '\0') return std::nan("");  // NEO_NO_COVERAGE (end==nullptr 不可达：strtod 不返 nullptr)
            // strtod 得 inf：若首字符非数字/符号 → 字面量误判（"infinity"/"INF"）→ NaN；
            // 若是数字/符号开头 → 合法溢出（"1e309"）→ 保留 inf。
            if (std::isinf(result)) {
                char c = s[i];
                if (c == '+' || c == '-') c = s[i + 1];  // 跳过符号看实际首字符
                if (c < '0' || c > '9') return std::nan("");  // 非数字开头 → 误判
            }
            return result;
        }
        default: return std::nan("");  // Exception/Fatal 不应走到这里（入口拦截）  NEO_NO_COVERAGE
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

    // 小数：用 std::to_string 后裁剪 trailing zeros
    std::string s = std::to_string(n);
    // 裁剪 trailing zeros（但至少保留一位小数）
    if (s.find('.') != std::string::npos) {  // NEO_NO_COVERAGE (else 不可达：std::to_string(double) 必含 '.')
        size_t last = s.find_last_not_of('0');
        if (s[last] == '.') {
            s.erase(last);       // "3.0" → "3"
        } else {
            s.erase(last + 1);   // "3.14000" → "3.14"
        }
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
        default: return "";  // Exception/Fatal 不应走到这里  NEO_NO_COVERAGE
    }
}

// —— 透传拦截（ADR-017 §4 Fatal 透传 + ADR-006 异常即值）——
// 行为方法入口检查：a/b 是 Fatal 或 Exception 时 return 该 Value，否则 return nullptr。
// Fatal 优先（比 Exception 更严重）。
inline Value* passthrough(Value* a, Value* b) {
    // a 是 Fatal/Exception 时分派走对应 Type::add，不会进入此函数 → a 的 Fatal/Exception 分支不可达
    if (a->type()->kind() == TypeKind::Fatal)     return a;  // NEO_NO_COVERAGE
    if (b->type()->kind() == TypeKind::Fatal)     return b;
    if (a->type()->kind() == TypeKind::Exception) return a;  // NEO_NO_COVERAGE
    if (b->type()->kind() == TypeKind::Exception) return b;
    return nullptr;
}

// 单目透传（toString 用）
inline Value* passthrough(Value* self) {
    TypeKind k = self->type()->kind();
    if (k == TypeKind::Fatal || k == TypeKind::Exception) return self;  // NEO_NO_COVERAGE (self 是该 Type 自己，非 Fatal/Exception)
    return nullptr;
}

// —— 内部调用结果检查（阶段5 前的 nullptr 防御 + Fatal 透传）——
// 行为方法内部调 ctx->createNumber/createString/add 后检查返回值。
// nullptr（OOM，阶段5 前）或 Fatal → 透传返回；正常 → 继续。
inline bool needPassthrough(Value* r) {
    return r == nullptr || r->type()->kind() == TypeKind::Fatal;  // NEO_NO_COVERAGE (阶段5 前 createNumber 不返 nullptr/Fatal)
}

}  // namespace neo::core::detail

#endif  // NEO_CORE_PRIVATE_TYPES_TYPE_BEHAVIOR_H
