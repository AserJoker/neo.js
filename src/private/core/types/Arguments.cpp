// types/Arguments.cpp - arguments exotic 对象实现（ES2024 §10.4.4）

#include "core/types/Arguments.h"

#include "core/IGCEngine.h"
#include "core/Scope.h"
#include "core/types/Array.h"  // parseArrayIndex
#include "core/types/Number.h"
#include "core/types/TypeBehavior.h"

#include <cmath>
#include <string>

namespace neo::core {

namespace {

// 键是数字索引且落在映射范围内时返回该索引，否则返回 false。
bool mappedIndex(ArgumentsData* d, Value* key, uint32_t& index) {
    if (key == nullptr || key->type()->kind() == TypeKind::Symbol) return false;
    std::string name = detail::keyToString(key);
    if (!parseArrayIndex(name, index)) return false;
    return index < d->mappedNames.size() && !d->mappedNames[index].empty();
}

}  // namespace

uint32_t ArgumentsType::indexedLength(Value* self) const {
    return static_cast<ArgumentsData*>(self->data())->length;
}

Value* ArgumentsType::indexedElement(IContext* ctx, Value* self,
                                     uint32_t index) const {
    auto* d = static_cast<ArgumentsData*>(self->data());
    if (index >= d->length) return nullptr;
    if (index >= d->mappedNames.size()) return nullptr;  // 普通属性存储
    const std::string& name = d->mappedNames[index];
    if (name.empty()) return nullptr;  // 映射已解除，回落普通属性
    if (d->scope != nullptr) {
        Scope::Binding* binding = d->scope->lookupBinding(name);
        if (binding != nullptr && binding->value != nullptr) {
            return binding->value;
        }
    }
    return ctx->createUndefined();
}

bool ArgumentsType::indexedWritable(Value* /*self*/) const { return true; }
bool ArgumentsType::indexedConfigurable(Value* /*self*/) const { return true; }
bool ArgumentsType::indexedLengthWritable(Value* self) const {
    return static_cast<ArgumentsData*>(self->data())->lengthWritable;
}

Value* ArgumentsType::defineProperty(IContext* ctx, Value* self, Value* key,
                                     Value* value, bool writable,
                                     bool enumerable, bool configurable) {
    auto* d = static_cast<ArgumentsData*>(self->data());
    // arguments.length 是普通可写数据属性（enumerable=false, configurable=true）。
    if (key != nullptr && key->type()->kind() != TypeKind::Symbol &&
        detail::keyToString(key) == "length") {
        if (enumerable) return ctx->createBoolean(false);
        Value* nv = detail::toNumberValue(ctx, value);
        if (detail::isErrorValue(nv)) return nv;
        double num = static_cast<NumberData*>(nv->data())->value;
        if (num >= 0 && num <= 4294967295.0 && num == std::trunc(num)) {
            if (!d->lengthWritable && static_cast<uint32_t>(num) != d->length) {
                return ctx->createBoolean(false);
            }
            d->length = static_cast<uint32_t>(num);
        }
        d->lengthWritable = writable;
        return ctx->createBoolean(true);
    }
    uint32_t index = 0;
    if (mappedIndex(d, key, index)) {
        // 保持完整可写特性的普通数据更新：只写回形参绑定，属性视图由
        // indexedElement 实时反映。其余描述符变更解除映射后走普通属性。
        if (writable && enumerable && configurable) {
            Value* stored =
                ctx->storeBinding(d->scope, d->mappedNames[index], value);
            if (detail::isErrorValue(stored)) return stored;
            return ctx->createBoolean(true);
        }
        // 映射参数：先写回形参绑定（§10.4.4.2 步骤 2.b），
        // writable=false 时解除映射，再走普通自有属性定义。
        if (value != nullptr) {
            Value* stored = ctx->storeBinding(d->scope, d->mappedNames[index], value);
            if (detail::isErrorValue(stored)) return stored;
        }
        if (!writable) d->mappedNames[index].clear();
    }
    return ObjectType::defineProperty(ctx, self, key, value, writable,
                                      enumerable, configurable);
}

Value* ArgumentsType::defineAccessor(IContext* ctx, Value* self, Value* key,
                                     Value* getter, Value* setter,
                                     bool enumerable, bool configurable) {
    auto* d = static_cast<ArgumentsData*>(self->data());
    uint32_t index = 0;
    if (mappedIndex(d, key, index)) {
        d->mappedNames[index].clear();
    }
    return ObjectType::defineAccessor(ctx, self, key, getter, setter, enumerable,
                                      configurable);
}

Value* ArgumentsType::deleteProperty(IContext* ctx, Value* self, Value* key) {
    auto* d = static_cast<ArgumentsData*>(self->data());
    uint32_t index = 0;
    if (mappedIndex(d, key, index)) {
        d->mappedNames[index].clear();
    }
    return ObjectType::deleteProperty(ctx, self, key);
}

ArgumentsData::~ArgumentsData() {
    if (scope == nullptr) return;
    Scope* env = scope;
    scope = nullptr;
    // 释放 pin：函数已返回时该作用域是 autoDispose 孤儿，计数归 0 即回收。
    if (env->decCaptureRef() && env->autoDispose()) {
        env->destroySelf();
    }
}

void ArgumentsData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
    if (callee != nullptr) visitor->visit(callee);
    // 逃逸的 arguments 是函数环境的唯一持有者：主动标记，否则孤儿作用域
    // 及其中的形参值会被回收成悬垂引用。
    if (scope != nullptr) visitor->visitScope(scope);
}

IType* getArgumentsType() {
    static ArgumentsType inst;
    return &inst;
}

}  // namespace neo::core
