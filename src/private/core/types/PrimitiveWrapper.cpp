// PrimitiveWrapper.cpp - PrimitiveWrapperType 行为方法 + PrimitiveWrapperData 实现
//
// 包装对象的所有对象行为委托 ObjectType（属性容器/原型链/访问器/环检测），
// 仅两处特化：
//   1. ToPrimitive（运算与 toString）：拆 [[PrimitiveValue]] 后按原始值规则分派；
//   2. String 包装的 length 与索引属性（ES2024 §10.4.3.1/§10.4.3.2）：
//      length 与索引是只读、不可配置、可枚举的自有属性。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）、019（赋值语义 §2）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Value.h"
#include "core/types/Array.h"  // parseArrayIndex
#include "core/types/PrimitiveWrapper.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "unicode/Utf8.h"

#include <cstdint>
#include <string>
#include <vector>

namespace neo::core {

namespace {

// 把 UTF-8/WTF-8 字符串拆成 UTF-16 码元序列（String 包装的索引单位）。
std::vector<uint16_t> toUtf16Units(const std::string& s) {
    std::vector<uint16_t> units;
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = 0;
        if (!unicode::decodeCodePoint(s, i, cp)) {
            units.push_back(static_cast<uint8_t>(s[i]));
            ++i;
            continue;
        }
        if (cp > 0xFFFF) {
            cp -= 0x10000;
            units.push_back(static_cast<uint16_t>(0xD800 + (cp >> 10)));
            units.push_back(static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            units.push_back(static_cast<uint16_t>(cp));
        }
    }
    return units;
}

// 单个 UTF-16 码元按 WTF-8 编码（孤立代理保留 3 字节形式）。
std::string unitToString(uint16_t unit) {
    std::string out;
    unicode::encodeCodePoint(unit, out);
    return out;
}

// 包装对象若是 String 包装，返回被包装的字符串；否则 nullptr。
const std::string* stringPayload(Value* self) {
    auto* d = static_cast<PrimitiveWrapperData*>(self->data());
    if (d->primitiveValue == nullptr ||
        d->primitiveValue->type()->kind() != TypeKind::String) {
        return nullptr;
    }
    return &static_cast<StringData*>(d->primitiveValue->data())->value;
}

// 键是否 symbol（symbol 走 symbols 容器，不做 ToPropertyKey）
inline bool isSymbolKeyValue(Value* key) {
    return key->type()->kind() == TypeKind::Symbol;
}

// String 包装的只读属性统一报错
Value* readOnlyProperty(IContext* ctx, const char* op) {
    std::string msg = std::string("Cannot ") + op +
                      " property of a String wrapper object";
    return detail::typeError(ctx, msg);
}

}  // namespace

// —— 内部槽访问 ——

Value* wrapperPrimitiveValue(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::PrimitiveWrapper) return nullptr;
    return static_cast<PrimitiveWrapperData*>(v->data())->primitiveValue;
}

const std::string* wrapperStringValue(Value* v) {
    if (v == nullptr || v->type()->kind() != TypeKind::PrimitiveWrapper) return nullptr;
    return stringPayload(v);
}

// —— PrimitiveWrapperData ——

void PrimitiveWrapperData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
    if (primitiveValue != nullptr) visitor->visit(primitiveValue);
}

IData* PrimitiveWrapperData::clone(neo::infra::IAllocator* alloc) const {
    // 包装对象是引用语义（ADR-019 §2）：clone 不复制容器，ref++ 后返回 this。
    return ObjectData::clone(alloc);
}

// —— 运算：走 ToPrimitive（ES2024 §13.15.3 / §13.15.4）——
// 包装对象经 OrdinaryToPrimitive 得 [[PrimitiveValue]]，故与原始值同规则。

Value* PrimitiveWrapperType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::addValues(ctx, a, b);
}

Value* PrimitiveWrapperType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '-', a, b);
}

Value* PrimitiveWrapperType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '*', a, b);
}

Value* PrimitiveWrapperType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '/', a, b);
}

Value* PrimitiveWrapperType::toString(IContext* ctx, Value* self) {
    // ToString(wrapper) = ToPrimitive(STRING) → valueOf/toString（ES2024 §7.1.14）
    return detail::toStringValue(ctx, self);
}

Value* PrimitiveWrapperType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                                  size_t /*argc*/, Value* /*argv*/[]) {
    return detail::notAFunction(ctx, self);
}

Value* PrimitiveWrapperType::construct(IContext* ctx, Value* self, size_t /*argc*/,
                                       Value* /*argv*/[]) {
    return detail::notAConstructor(ctx, self);
}

// —— 属性操作 ——

// String 包装的 length/索引判定（ES2024 §10.4.3.1/§10.4.3.2）：
// 先 ToPropertyKey（§7.1.19），故对象键同样可命中。
// 返回 true 表示键已转换（k 有效），matches 指出是否命中只读内部属性；
// 键转换失败时返回 false 并写入 err（调用方透传错误）。
bool resolveStringWrapperKey(IContext* ctx, const std::string* s, Value* key,
                             std::string& k, bool& matches, Value*& err) {
    matches = false;
    err = nullptr;
    if (s == nullptr || isSymbolKeyValue(key)) return false;
    if (Value* e = detail::propertyKeyString(ctx, key, k)) {
        err = e;
        return false;
    }
    if (k == "length") {
        matches = true;
        return true;
    }
    uint32_t index = 0;
    if (parseArrayIndex(k, index) && index < toUtf16Units(*s).size()) {
        matches = true;
    }
    return true;
}

// —— 索引型 exotic 能力（仅 String 包装有；Boolean/Number/Symbol 包装返回空）——

bool PrimitiveWrapperType::hasIndexedElements() const { return true; }

bool PrimitiveWrapperType::hasIndexedLength(Value* self) const {
    return stringPayload(self) != nullptr;
}

uint32_t PrimitiveWrapperType::indexedLength(Value* self) const {
    const std::string* s = stringPayload(self);
    return s != nullptr ? static_cast<uint32_t>(toUtf16Units(*s).size()) : 0;
}

Value* PrimitiveWrapperType::indexedElement(IContext* ctx, Value* self,
                                            uint32_t index) const {
    const std::string* s = stringPayload(self);
    if (s == nullptr) return nullptr;
    const std::vector<uint16_t> units = toUtf16Units(*s);
    if (index >= units.size()) return nullptr;
    return ctx->createString(unitToString(units[index]));
}

// String 包装的 length/索引自有属性（§10.4.3.1/§10.4.3.2）：
// length 不可写/不可枚举/不可配置；索引不可写/可枚举/不可配置。
Value* PrimitiveWrapperType::getOwnProperty(IContext* ctx, Value* self,
                                            const PropertyKey& key, bool& exists,
                                            Property& out) const {
    const std::string* s = stringPayload(self);
    if (s != nullptr && !key.isSymbol) {
        const std::vector<uint16_t> units = toUtf16Units(*s);
        if (key.name == "length") {
            exists = true;
            out = Property{};
            out.value = ctx->createNumber(static_cast<double>(units.size()));
            out.writable = false;
            out.enumerable = false;
            out.configurable = false;
            return nullptr;
        }
        uint32_t index = 0;
        if (parseArrayIndex(key.name, index) && index < units.size()) {
            exists = true;
            out = Property{};
            out.value = ctx->createString(unitToString(units[index]));
            out.writable = false;
            out.enumerable = true;
            out.configurable = false;
            return nullptr;
        }
    }
    return IType::getOwnProperty(ctx, self, key, exists, out);
}

Value* PrimitiveWrapperType::getProperty(IContext* ctx, Value* self, Value* key) {
    const std::string* s = stringPayload(self);
    std::string k;
    bool matches = false;
    Value* err = nullptr;
    if (resolveStringWrapperKey(ctx, s, key, k, matches, err)) {
        if (matches) {
            const std::vector<uint16_t> units = toUtf16Units(*s);
            if (k == "length") {
                return ctx->createNumber(static_cast<double>(units.size()));
            }
            uint32_t index = 0;
            parseArrayIndex(k, index);
            return ctx->createString(unitToString(units[index]));
        }
    } else if (err != nullptr) {
        return err;
    }
    return getObjectType()->getProperty(ctx, self, key);
}

Value* PrimitiveWrapperType::setProperty(IContext* ctx, Value* self, Value* key,
                                         Value* value) {
    std::string k;
    bool matches = false;
    Value* err = nullptr;
    if (resolveStringWrapperKey(ctx, stringPayload(self), key, k, matches, err)) {
        if (matches) {
            // sloppy 模式下给字符串索引赋值静默失败（[[Set]] 返回 false）。
            if (!ctx->hasFlag("strict")) return value;
            return readOnlyProperty(ctx, "assign to read only");
        }
    } else if (err != nullptr) {
        return err;
    }
    return getObjectType()->setProperty(ctx, self, key, value);
}

Value* PrimitiveWrapperType::hasProperty(IContext* ctx, Value* self, Value* key) {
    std::string k;
    bool matches = false;
    Value* err = nullptr;
    if (resolveStringWrapperKey(ctx, stringPayload(self), key, k, matches, err)) {
        if (matches) return ctx->createBoolean(true);
    } else if (err != nullptr) {
        return err;
    }
    return getObjectType()->hasProperty(ctx, self, key);
}

Value* PrimitiveWrapperType::deleteProperty(IContext* ctx, Value* self, Value* key) {
    std::string k;
    bool matches = false;
    Value* err = nullptr;
    if (resolveStringWrapperKey(ctx, stringPayload(self), key, k, matches, err)) {
        if (matches) return readOnlyProperty(ctx, "delete");
    } else if (err != nullptr) {
        return err;
    }
    return getObjectType()->deleteProperty(ctx, self, key);
}

Value* PrimitiveWrapperType::defineProperty(IContext* ctx, Value* self, Value* key,
                                            Value* value, bool writable, bool enumerable,
                                            bool configurable) {
    std::string k;
    bool matches = false;
    Value* err = nullptr;
    if (resolveStringWrapperKey(ctx, stringPayload(self), key, k, matches, err)) {
        if (matches) {
            // String exotic [[DefineOwnProperty]]（§10.4.3.5）：索引/length 只读、
            // 不可配置；仅接受与当前描述符一致的重新定义。
            const std::vector<uint16_t> units = toUtf16Units(*stringPayload(self));
            Value* current = nullptr;
            bool currentEnumerable = false;
            if (k == "length") {
                current = ctx->createNumber(static_cast<double>(units.size()));
            } else {
                uint32_t index = 0;
                parseArrayIndex(k, index);
                current = ctx->createString(unitToString(units[index]));
                currentEnumerable = true;
            }
            if (configurable || writable || enumerable != currentEnumerable ||
                !detail::sameValue(value, current)) {
                return ctx->createBoolean(false);
            }
            return ctx->createBoolean(true);
        }
    } else if (err != nullptr) {
        return err;
    }
    return getObjectType()->defineProperty(ctx, self, key, value, writable, enumerable,
                                           configurable);
}

Value* PrimitiveWrapperType::defineAccessor(IContext* ctx, Value* self, Value* key,
                                            Value* getter, Value* setter, bool enumerable,
                                            bool configurable) {
    std::string k;
    bool matches = false;
    Value* err = nullptr;
    if (resolveStringWrapperKey(ctx, stringPayload(self), key, k, matches, err)) {
        if (matches) return ctx->createBoolean(false);  // 数据属性不可转为访问器
    } else if (err != nullptr) {
        return err;
    }
    return getObjectType()->defineAccessor(ctx, self, key, getter, setter, enumerable,
                                           configurable);
}

Value* PrimitiveWrapperType::getPrototype(IContext* ctx, Value* self) {
    return getObjectType()->getPrototype(ctx, self);
}

Value* PrimitiveWrapperType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    return getObjectType()->setPrototype(ctx, self, proto);
}

}  // namespace neo::core
