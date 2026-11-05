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

#include <string>

namespace neo::core {

namespace {

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
    return ctx->createException(ctx->createString(msg));
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
    if (parseArrayIndex(k, index) && index < s->size()) matches = true;
    return true;
}

Value* PrimitiveWrapperType::getProperty(IContext* ctx, Value* self, Value* key) {
    const std::string* s = stringPayload(self);
    std::string k;
    bool matches = false;
    Value* err = nullptr;
    if (resolveStringWrapperKey(ctx, s, key, k, matches, err)) {
        if (matches) {
            if (k == "length") return ctx->createNumber(static_cast<double>(s->size()));
            uint32_t index = 0;
            parseArrayIndex(k, index);
            return ctx->createString(std::string(1, (*s)[index]));
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
        if (matches) return readOnlyProperty(ctx, "assign to read only");
    } else if (err != nullptr) {
        return err;
    }
    return getObjectType()->setProperty(ctx, self, key, value);
}

bool PrimitiveWrapperType::hasProperty(IContext* ctx, Value* self, Value* key) {
    std::string k;
    bool matches = false;
    Value* err = nullptr;
    if (resolveStringWrapperKey(ctx, stringPayload(self), key, k, matches, err)) {
        if (matches) return true;
    } else if (err != nullptr) {
        return false;  // ToPropertyKey 失败 → 不存在
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
        if (matches) return readOnlyProperty(ctx, "redefine");
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
        if (matches) return readOnlyProperty(ctx, "redefine");
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
