// Array.cpp - ArrayType 行为方法 + ArrayData 元素容器实现
//
// 索引元素按 idx -> Property 存储（见 Array.h）：正确性优先，不做稠密优化。
// 命名属性/符号属性/原型链全部委托 ObjectType（ArrayData 继承 ObjectData）。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型 §6）、019（赋值语义 §2）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/TypeBehavior.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace neo::core {

namespace {

// 键是否 symbol（symbol 走 symbols 容器，不做 ToPropertyKey）
inline bool isSymbolKeyValue(Value* key) {
    return key->type()->kind() == TypeKind::Symbol;
}

// 创建 "Invalid array length" 异常
Value* invalidLength(IContext* ctx) {
    return detail::rangeError(ctx, "Invalid array length");
}

}  // namespace

// —— 索引键解析 ——

bool parseArrayIndex(const std::string& key, uint32_t& out) {
    if (key.empty()) return false;
    // 无前导零（"0" 本身除外）
    if (key.size() > 1 && key[0] == '0') return false;
    uint64_t value = 0;
    for (char c : key) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint64_t>(c - '0');
        if (value >= kArrayMaxLength) return false;  // 索引须 < 2^32-1
    }
    out = static_cast<uint32_t>(value);
    return true;
}

// —— ArrayData ——

Value* ArrayData::elementAt(uint32_t index) const {
    auto it = elements.find(index);
    if (it == elements.end() || isAccessorDescriptor(it->second)) return nullptr;
    return it->second.value;
}

bool ArrayData::hasElement(uint32_t index) const {
    return elements.find(index) != elements.end();
}

void ArrayData::setElement(uint32_t index, Value* value) {
    elements[index] = Property{value, true, true, true, nullptr, nullptr};
    if (index + 1 > length_) length_ = index + 1;
}

bool ArrayData::removeElement(uint32_t index) {
    return elements.erase(index) != 0;
}

void ArrayData::setLength(uint32_t n) {
    for (auto it = elements.begin(); it != elements.end();) {
        if (it->first >= n) {
            it = elements.erase(it);
        } else {
            ++it;
        }
    }
    length_ = n;
}

void ArrayData::enumerateChildren(IGCVisitor* visitor) {
    // 继承的对象字段：prototype + 命名属性 + 符号属性 + 私有槽
    ObjectData::enumerateChildren(visitor);
    // 索引属性：value/getter/setter 全部保活。
    for (auto& [index, prop] : elements) {
        (void)index;
        if (prop.value != nullptr) visitor->visit(prop.value);
        if (prop.getter != nullptr) visitor->visit(prop.getter);
        if (prop.setter != nullptr) visitor->visit(prop.setter);
    }
}

IData* ArrayData::clone(neo::infra::IAllocator* alloc) const {
    // 数组是引用语义（ADR-019 §2）：clone 不复制容器，ref++ 后返回 this。
    return ObjectData::clone(alloc);
}

std::string arrayToString(const ArrayData* d) {
    std::string result;
    for (uint32_t i = 0; i < d->length(); ++i) {
        if (i > 0) result += ',';
        Value* v = d->elementAt(i);
        if (v == nullptr) continue;  // 空洞 → 空串
        TypeKind k = v->type()->kind();
        if (k == TypeKind::Undefined || k == TypeKind::Null) continue;
        result += detail::valueToString(v);
    }
    return result;
}

// —— ArrayType 行为方法 ——

// 数组运算走 ToPrimitive（ES2024 §13.15.3 / §13.15.4）：
// 注：属性键统一经 ToPropertyKey（见 resolveKey），对象键可触发用户 valueOf/toString。
// Array.prototype.toString → join(",")，故 + 为字符串拼接、算术按 join 结果转数字。

Value* ArrayType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::addValues(ctx, a, b);
}

Value* ArrayType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '-', a, b);
}

Value* ArrayType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '*', a, b);
}

Value* ArrayType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '/', a, b);
}

Value* ArrayType::toString(IContext* ctx, Value* self) {
    // ToString(array) = ToPrimitive(STRING) → join(",")（ES2024 §7.1.14）
    return detail::toStringValue(ctx, self);
}

Value* ArrayType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                       size_t /*argc*/, Value* /*argv*/[]) {
    return detail::notAFunction(ctx, self);
}

Value* ArrayType::construct(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return detail::notAConstructor(ctx, self);
}

// —— 索引型 exotic 能力（供对象模型枚举/自有属性判定复用）——

uint32_t ArrayType::indexedLength(Value* self) const {
    return static_cast<ArrayData*>(self->data())->length();
}

bool ArrayType::indexedLengthWritable(Value* self) const {
    return static_cast<ArrayData*>(self->data())->lengthWritable;
}

Value* ArrayType::indexedElement(IContext* /*ctx*/, Value* self,
                                 uint32_t index) const {
    return static_cast<ArrayData*>(self->data())->elementAt(index);
}

// 索引自有属性 / length：Array 自己维护；其余走普通对象算法。
Value* ArrayType::getOwnProperty(IContext* ctx, Value* self, const PropertyKey& key,
                                 bool& exists, Property& out) const {
    exists = false;
    auto* d = static_cast<ArrayData*>(self->data());
    if (!key.isSymbol) {
        if (key.name == "length") {
            Value* len = ctx->createNumber(static_cast<double>(d->length_));
            if (len == ctx->oomValue()) return len;
            out = Property{len, d->lengthWritable, false, false, nullptr, nullptr};
            exists = true;
            return nullptr;
        }
        uint32_t index = 0;
        if (parseArrayIndex(key.name, index)) {
            auto it = d->elements.find(index);
            if (it != d->elements.end()) {
                out = it->second;
                exists = true;
            }
            return nullptr;
        }
    }
    return getObjectType()->getOwnProperty(ctx, self, key, exists, out);
}

Value* ArrayType::ownKeys(IContext* ctx, Value* self, std::vector<Value*>& out) const {
    auto* d = static_cast<ArrayData*>(self->data());
    std::vector<uint32_t> indices;
    indices.reserve(d->elements.size());
    for (const auto& [index, prop] : d->elements) {
        (void)prop;
        indices.push_back(index);
    }
    std::sort(indices.begin(), indices.end());
    for (uint32_t index : indices) {
        Value* k = ctx->createString(std::to_string(index));
        if (k == ctx->oomValue()) return k;
        out.push_back(k);
    }
    Value* lenKey = ctx->createString("length");
    if (lenKey == ctx->oomValue()) return lenKey;
    out.push_back(lenKey);
    for (const std::string& name : d->order) {
        uint32_t index = 0;
        if (parseArrayIndex(name, index)) continue;
        if (d->properties.find(name) == d->properties.end()) continue;
        Value* k = ctx->createString(name);
        if (k == ctx->oomValue()) return k;
        out.push_back(k);
    }
    for (const void* ident : d->symbolOrder) {
        auto it = d->symbols.find(ident);
        if (it == d->symbols.end()) continue;
        out.push_back(it->second.key);
    }
    return nullptr;
}

Value* ArrayType::getProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->getProperty(ctx, self, key);
}

Value* ArrayType::setProperty(IContext* ctx, Value* self, Value* key, Value* value) {
    return getObjectType()->setProperty(ctx, self, key, value);
}

Value* ArrayType::hasProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->hasProperty(ctx, self, key);
}

Value* ArrayType::deleteProperty(IContext* ctx, Value* self, Value* key) {
    std::string k;
    if (!isSymbolKeyValue(key)) {
        if (Value* err = detail::propertyKeyString(ctx, key, k)) return err;
        if (k == "length") return ctx->createBoolean(false);
        uint32_t index = 0;
        if (parseArrayIndex(k, index)) {
            auto* d = static_cast<ArrayData*>(self->data());
            auto it = d->elements.find(index);
            if (it == d->elements.end()) return ctx->createBoolean(true);
            if (!it->second.configurable) return ctx->createBoolean(false);
            d->elements.erase(it);
            return ctx->createBoolean(true);
        }
    }
    return getObjectType()->deleteProperty(ctx, self, key);
}

// ArraySetLength（ES2024 §10.4.2.4）：缩短时逐个删除索引，
// 遇到不可配置属性则失败并把 length 设为该索引 + 1。
Value* ArrayType::defineProperty(IContext* ctx, Value* self, Value* key,
                                 Value* value, bool writable, bool enumerable,
                                 bool configurable) {
    auto* d = static_cast<ArrayData*>(self->data());
    std::string k;
    if (!isSymbolKeyValue(key)) {
        if (Value* err = detail::propertyKeyString(ctx, key, k)) return err;
        if (k == "length") {
            if (enumerable || configurable) return ctx->createBoolean(false);
            if (!writable && d->lengthWritable) {
                d->lengthWritable = false;
            } else if (writable && !d->lengthWritable) {
                return ctx->createBoolean(false);
            }
            Value* nv = detail::toNumberValue(ctx, value);
            if (detail::isErrorValue(nv)) return nv;
            double num = static_cast<NumberData*>(nv->data())->value;
            uint32_t newLen = 0;
            if (!std::isnan(num) && !std::isinf(num) && num != 0.0) {
                double mod = std::fmod(std::trunc(num), 4294967296.0);
                if (mod < 0.0) mod += 4294967296.0;
                newLen = static_cast<uint32_t>(mod);
            }
            if (static_cast<double>(newLen) != num) return invalidLength(ctx);
            if (!d->lengthWritable && newLen != d->length_) {
                return ctx->createBoolean(false);
            }
            if (newLen < d->length_) {
                // 从高到低删除；不可配置索引阻断截断。
                uint32_t blocked = 0;
                bool hasBlocked = false;
                for (auto it = d->elements.begin(); it != d->elements.end();) {
                    if (it->first >= newLen) {
                        if (!it->second.configurable) {
                            if (!hasBlocked || it->first > blocked) {
                                blocked = it->first;
                                hasBlocked = true;
                            }
                            ++it;
                        } else {
                            it = d->elements.erase(it);
                        }
                    } else {
                        ++it;
                    }
                }
                if (hasBlocked) {
                    d->length_ = blocked + 1;
                    return ctx->createBoolean(false);
                }
            }
            d->length_ = newLen;
            return ctx->createBoolean(true);
        }
        uint32_t index = 0;
        if (parseArrayIndex(k, index)) {
            Property desc{value, writable, enumerable, configurable, nullptr, nullptr};
            auto it = d->elements.find(index);
            if (it != d->elements.end()) {
                if (!isCompatiblePropertyDescriptor(it->second, desc)) {
                    return ctx->createBoolean(false);
                }
                it->second = desc;
            } else {
                if (!d->extensible) return ctx->createBoolean(false);
                d->elements.emplace(index, desc);
            }
            if (index + 1 > d->length_) d->length_ = index + 1;
            return ctx->createBoolean(true);
        }
    }
    return getObjectType()->defineProperty(ctx, self, key, value, writable, enumerable,
                                           configurable);
}

Value* ArrayType::defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                                 Value* setter, bool enumerable, bool configurable) {
    auto* d = static_cast<ArrayData*>(self->data());
    std::string k;
    if (!isSymbolKeyValue(key)) {
        if (Value* err = detail::propertyKeyString(ctx, key, k)) return err;
        if (k == "length") return ctx->createBoolean(false);
        uint32_t index = 0;
        if (parseArrayIndex(k, index)) {
            Property desc{nullptr, false, enumerable, configurable, getter, setter};
            auto it = d->elements.find(index);
            if (it != d->elements.end()) {
                if (!isCompatiblePropertyDescriptor(it->second, desc)) {
                    return ctx->createBoolean(false);
                }
                it->second = desc;
            } else {
                if (!d->extensible) return ctx->createBoolean(false);
                d->elements.emplace(index, desc);
            }
            if (index + 1 > d->length_) d->length_ = index + 1;
            return ctx->createBoolean(true);
        }
    }
    return getObjectType()->defineAccessor(ctx, self, key, getter, setter, enumerable,
                                           configurable);
}

Value* ArrayType::getPrototype(IContext* ctx, Value* self) {
    return getObjectType()->getPrototype(ctx, self);
}

Value* ArrayType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    return getObjectType()->setPrototype(ctx, self, proto);
}

}  // namespace neo::core
