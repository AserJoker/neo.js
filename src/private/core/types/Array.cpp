// Array.cpp - ArrayType 行为方法 + ArrayData 元素容器实现
//
// 索引元素混合存储（见 Array.h）：小数组 vector（nullptr=空洞），超阈值迁移 map。
// 命名属性/符号属性/原型链全部委托 ObjectType（ArrayData 继承 ObjectData）。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型 §6）、019（赋值语义 §2）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/TypeBehavior.h"

#include <cmath>
#include <string>

namespace neo::core {

namespace {

// 键是否为字符串 "length"
inline bool isLengthKey(Value* key) {
    return key->type()->kind() == TypeKind::String &&
           static_cast<StringData*>(key->data())->value == "length";
}

// 创建 "Invalid array length" 异常
Value* invalidLength(IContext* ctx) {
    return ctx->createException(ctx->createString("Invalid array length"));
}

// 沿原型链查找索引元素（含 self 自身）。
// 链上节点可能是 Array（查 elements）或 Object/Function（无 elements，跳过该层）。
Value* findIndexInChain(Value* self, uint32_t index) {
    for (Value* cur = self; cur != nullptr;) {
        TypeKind k = cur->type()->kind();
        if (k == TypeKind::Array) {
            auto* d = static_cast<ArrayData*>(cur->data());
            if (Value* v = d->elementAt(index)) return v;
            cur = d->prototype;
            continue;
        }
        if (k == TypeKind::Object || k == TypeKind::Function) {
            cur = static_cast<ObjectData*>(cur->data())->prototype;
            continue;
        }
        return nullptr;
    }
    return nullptr;
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
    if (sparseMode) {
        auto it = sparse.find(index);
        return it == sparse.end() ? nullptr : it->second;
    }
    return index < dense.size() ? dense[index] : nullptr;
}

bool ArrayData::hasElement(uint32_t index) const {
    return elementAt(index) != nullptr;
}

void ArrayData::toSparse() {
    if (sparseMode) return;
    sparse.reserve(dense.size());
    for (size_t i = 0; i < dense.size(); ++i) {
        if (dense[i] != nullptr) {
            sparse[static_cast<uint32_t>(i)] = dense[i];
        }
    }
    dense.clear();
    dense.shrink_to_fit();
    sparseMode = true;
}

void ArrayData::setElement(uint32_t index, Value* value) {
    // 超过稠密阈值 → 一次性迁移到稀疏存储（不回切）
    if (!sparseMode && index >= kArrayDenseLimit) toSparse();

    if (sparseMode) {
        sparse[index] = value;
        if (index + 1 > length_) length_ = index + 1;
        return;
    }
    if (index >= dense.size()) {
        dense.resize(static_cast<size_t>(index) + 1, nullptr);
    }
    dense[index] = value;
    length_ = static_cast<uint32_t>(dense.size());
}

bool ArrayData::removeElement(uint32_t index) {
    if (sparseMode) {
        return sparse.erase(index) != 0;
    }
    if (index >= dense.size() || dense[index] == nullptr) return false;
    dense[index] = nullptr;  // 置空洞，length 不变
    return true;
}

void ArrayData::setLength(uint32_t n) {
    if (!sparseMode && n > kArrayDenseLimit) toSparse();

    if (sparseMode) {
        for (auto it = sparse.begin(); it != sparse.end();) {
            if (it->first >= n) {
                it = sparse.erase(it);
            } else {
                ++it;
            }
        }
    } else {
        dense.resize(n, nullptr);  // 截断或补空洞
    }
    length_ = n;
}

void ArrayData::enumerateChildren(IGCVisitor* visitor) {
    // 继承的对象字段：prototype + 命名属性 + 符号属性
    ObjectData::enumerateChildren(visitor);
    // 索引元素（空洞为 nullptr，跳过）
    if (sparseMode) {
        for (auto& [index, value] : sparse) {
            (void)index;
            if (value != nullptr) visitor->visit(value);
        }
        return;
    }
    for (Value* v : dense) {
        if (v != nullptr) visitor->visit(v);
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

Value* ArrayType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    // 包装对象先 ToPrimitive 拆出原始值，再重新分派（[1] + new Number(2) === "12"）
    if (b->type()->kind() == TypeKind::PrimitiveWrapper) {
        return ctx->add(a, static_cast<PrimitiveWrapperData*>(b->data())->primitiveValue);
    }
    // ToPrimitive(array) 是字符串，故 + 恒为字符串拼接（[1,2] + 3 === "1,23"）
    std::string lhs = arrayToString(static_cast<ArrayData*>(a->data()));
    return ctx->createString(lhs + detail::valueToString(b));
}

Value* ArrayType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    double lhs = detail::stringToNumber(arrayToString(static_cast<ArrayData*>(a->data())));
    return ctx->createNumber(lhs - detail::toNumber(b));
}

Value* ArrayType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    double lhs = detail::stringToNumber(arrayToString(static_cast<ArrayData*>(a->data())));
    return ctx->createNumber(lhs * detail::toNumber(b));
}

Value* ArrayType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    double lhs = detail::stringToNumber(arrayToString(static_cast<ArrayData*>(a->data())));
    return ctx->createNumber(lhs / detail::toNumber(b));
}

Value* ArrayType::toString(IContext* ctx, Value* self) {
    return ctx->createString(arrayToString(static_cast<ArrayData*>(self->data())));
}

Value* ArrayType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                       size_t /*argc*/, Value* /*argv*/[]) {
    return detail::notAFunction(ctx, self);
}

Value* ArrayType::construct(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return detail::notAConstructor(ctx, self);
}

Value* ArrayType::getProperty(IContext* ctx, Value* self, Value* key) {
    if (isLengthKey(key)) {
        return ctx->createNumber(static_cast<double>(static_cast<ArrayData*>(self->data())->length()));
    }
    if (key->type()->kind() != TypeKind::Symbol) {
        uint32_t index = 0;
        if (parseArrayIndex(detail::keyToString(key), index)) {
            // 沿原型链查找索引元素（空洞则继续向上，与普通属性查找一致）
            if (Value* v = findIndexInChain(self, index)) return v;
        }
    }
    return getObjectType()->getProperty(ctx, self, key);
}

Value* ArrayType::setProperty(IContext* ctx, Value* self, Value* key, Value* value) {
    auto* d = static_cast<ArrayData*>(self->data());
    if (isLengthKey(key)) {
        // ArraySetLength：值须为 0..2^32-1 的整数
        if (value->type()->kind() != TypeKind::Number) return invalidLength(ctx);
        double n = static_cast<NumberData*>(value->data())->value;
        if (!std::isfinite(n) || n < 0.0 || n > static_cast<double>(kArrayMaxLength) ||
            std::floor(n) != n) {
            return invalidLength(ctx);
        }
        d->setLength(static_cast<uint32_t>(n));
        return value;
    }
    if (key->type()->kind() != TypeKind::Symbol) {
        uint32_t index = 0;
        if (parseArrayIndex(detail::keyToString(key), index)) {
            d->setElement(index, value);
            return value;
        }
    }
    return getObjectType()->setProperty(ctx, self, key, value);
}

bool ArrayType::hasProperty(IContext* ctx, Value* self, Value* key) {
    if (isLengthKey(key)) return true;
    if (key->type()->kind() != TypeKind::Symbol) {
        uint32_t index = 0;
        if (parseArrayIndex(detail::keyToString(key), index) &&
            findIndexInChain(self, index) != nullptr) {
            return true;
        }
    }
    return getObjectType()->hasProperty(ctx, self, key);
}

Value* ArrayType::deleteProperty(IContext* ctx, Value* self, Value* key) {
    if (isLengthKey(key)) {
        return ctx->createException(ctx->createString("Cannot delete property 'length'"));
    }
    if (key->type()->kind() != TypeKind::Symbol) {
        uint32_t index = 0;
        if (parseArrayIndex(detail::keyToString(key), index)) {
            static_cast<ArrayData*>(self->data())->removeElement(index);
            return ctx->createBoolean(true);
        }
    }
    return getObjectType()->deleteProperty(ctx, self, key);
}

Value* ArrayType::defineProperty(IContext* ctx, Value* self, Value* key,
                                 Value* value, bool writable, bool enumerable,
                                 bool configurable) {
    if (isLengthKey(key)) {
        // length 不可配置、不可枚举；writable 允许（本阶段忽略其后续约束）
        if (enumerable || configurable) {
            return ctx->createException(
                ctx->createString("Cannot redefine property: length"));
        }
        return setProperty(ctx, self, key, value);
    }
    if (key->type()->kind() != TypeKind::Symbol) {
        uint32_t index = 0;
        if (parseArrayIndex(detail::keyToString(key), index)) {
            // 本阶段索引元素恒为 writable/enumerable/configurable 的数据属性
            if (!writable || !enumerable || !configurable) {
                return ctx->createException(ctx->createString(
                    "Array index property attributes are not supported"));
            }
            static_cast<ArrayData*>(self->data())->setElement(index, value);
            return value;
        }
    }
    return getObjectType()->defineProperty(ctx, self, key, value, writable, enumerable,
                                           configurable);
}

Value* ArrayType::defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                                 Value* setter, bool enumerable, bool configurable) {
    if (key->type()->kind() != TypeKind::Symbol) {
        uint32_t index = 0;
        if (parseArrayIndex(detail::keyToString(key), index) || isLengthKey(key)) {
            return ctx->createException(ctx->createString(
                "Cannot define accessor on array index or length"));
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
