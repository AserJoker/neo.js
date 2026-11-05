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

// 键是否为字符串 "length"
inline bool isLengthKey(Value* key) {
    return key->type()->kind() == TypeKind::String &&
           static_cast<StringData*>(key->data())->value == "length";
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

// 索引键解析（非 symbol 键）
inline bool indexOfKey(Value* key, uint32_t& out) {
    if (key->type()->kind() == TypeKind::Symbol) return false;
    return parseArrayIndex(detail::keyToString(key), out);
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

// —— 运算：ToPrimitive 后按原始值规则分派 ——

Value* PrimitiveWrapperType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return ctx->add(static_cast<PrimitiveWrapperData*>(a->data())->primitiveValue, b);
}

Value* PrimitiveWrapperType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return ctx->sub(static_cast<PrimitiveWrapperData*>(a->data())->primitiveValue, b);
}

Value* PrimitiveWrapperType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return ctx->mul(static_cast<PrimitiveWrapperData*>(a->data())->primitiveValue, b);
}

Value* PrimitiveWrapperType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return ctx->div(static_cast<PrimitiveWrapperData*>(a->data())->primitiveValue, b);
}

Value* PrimitiveWrapperType::toString(IContext* ctx, Value* self) {
    return ctx->toString(static_cast<PrimitiveWrapperData*>(self->data())->primitiveValue);
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

Value* PrimitiveWrapperType::getProperty(IContext* ctx, Value* self, Value* key) {
    if (const std::string* s = stringPayload(self)) {
        if (isLengthKey(key)) {
            return ctx->createNumber(static_cast<double>(s->size()));
        }
        uint32_t index = 0;
        if (indexOfKey(key, index) && index < s->size()) {
            return ctx->createString(std::string(1, (*s)[index]));
        }
    }
    return getObjectType()->getProperty(ctx, self, key);
}

Value* PrimitiveWrapperType::setProperty(IContext* ctx, Value* self, Value* key,
                                         Value* value) {
    if (const std::string* s = stringPayload(self)) {
        uint32_t index = 0;
        if (isLengthKey(key) || (indexOfKey(key, index) && index < s->size())) {
            return readOnlyProperty(ctx, "assign to read only");
        }
    }
    return getObjectType()->setProperty(ctx, self, key, value);
}

bool PrimitiveWrapperType::hasProperty(IContext* ctx, Value* self, Value* key) {
    if (const std::string* s = stringPayload(self)) {
        if (isLengthKey(key)) return true;
        uint32_t index = 0;
        if (indexOfKey(key, index) && index < s->size()) return true;
    }
    return getObjectType()->hasProperty(ctx, self, key);
}

Value* PrimitiveWrapperType::deleteProperty(IContext* ctx, Value* self, Value* key) {
    if (const std::string* s = stringPayload(self)) {
        uint32_t index = 0;
        if (isLengthKey(key) || (indexOfKey(key, index) && index < s->size())) {
            return readOnlyProperty(ctx, "delete");
        }
    }
    return getObjectType()->deleteProperty(ctx, self, key);
}

Value* PrimitiveWrapperType::defineProperty(IContext* ctx, Value* self, Value* key,
                                            Value* value, bool writable, bool enumerable,
                                            bool configurable) {
    if (const std::string* s = stringPayload(self)) {
        uint32_t index = 0;
        if (isLengthKey(key) || (indexOfKey(key, index) && index < s->size())) {
            return readOnlyProperty(ctx, "redefine");
        }
    }
    return getObjectType()->defineProperty(ctx, self, key, value, writable, enumerable,
                                           configurable);
}

Value* PrimitiveWrapperType::defineAccessor(IContext* ctx, Value* self, Value* key,
                                            Value* getter, Value* setter, bool enumerable,
                                            bool configurable) {
    if (const std::string* s = stringPayload(self)) {
        uint32_t index = 0;
        if (isLengthKey(key) || (indexOfKey(key, index) && index < s->size())) {
            return readOnlyProperty(ctx, "redefine");
        }
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
