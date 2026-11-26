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

Value* ArgumentsType::getOwnProperty(IContext* ctx, Value* self,
                                     const PropertyKey& key, bool& exists,
                                     Property& out) const {
    auto* d = static_cast<ArgumentsData*>(self->data());
    if (!key.isSymbol) {
        // length 是普通数据属性：writable=true, enumerable=false,
        // configurable=true（§10.4.4.1），与 Array.length 不同。
        if (key.name == "length") {
            if (d->lengthDeleted) return nullptr;
            auto it = d->properties.find("length");
            if (it != d->properties.end()) {
                out = it->second;
                exists = true;
                return nullptr;
            }
            Value* len = ctx->createNumber(static_cast<double>(d->length));
            if (len == ctx->oomValue()) return len;
            out = Property{len, d->lengthWritable, false, true, nullptr, nullptr};
            exists = true;
            return nullptr;
        }
        uint32_t index = 0;
        if (parseArrayIndex(key.name, index)) {
            auto it = d->properties.find(key.name);
            if (it != d->properties.end()) {
                // 已收紧特性/解除映射后以普通属性为准；映射仍有效时值实时
                // 取自形参绑定（§10.4.4.1 [[GetOwnProperty]]）。
                out = it->second;
                const bool mapped =
                    index < d->mappedNames.size() &&
                    !d->mappedNames[index].empty();
                if (mapped) {
                    Value* v = indexedElement(ctx, self, index);
                    if (v != nullptr) out.value = v;
                }
                exists = true;
                return nullptr;
            }
        }
    }
    return ObjectType::getOwnProperty(ctx, self, key, exists, out);
}

Value* ArgumentsType::defineProperty(IContext* ctx, Value* self, Value* key,
                                     Value* value, bool writable,
                                     bool enumerable, bool configurable) {
    auto* d = static_cast<ArgumentsData*>(self->data());
    if (key != nullptr && key->type()->kind() != TypeKind::Symbol &&
        detail::keyToString(key) == "length") {
        d->lengthDeleted = false;
        d->lengthWritable = writable;
    }
    uint32_t index = 0;
    if (mappedIndex(d, key, index)) {
        // 保持完整可写特性的普通数据更新：只写回形参绑定，属性视图由
        // indexedElement 实时反映。其余描述符变更解除映射后走普通属性。
        if (writable && enumerable && configurable &&
            d->properties.count(detail::keyToString(key)) == 0) {
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
    if (key != nullptr && key->type()->kind() != TypeKind::Symbol &&
        detail::keyToString(key) == "length") {
        d->lengthDeleted = true;
    }
    Value* result = ObjectType::deleteProperty(ctx, self, key);
    if (detail::isErrorValue(result)) return result;
    // 删除成功才解除映射；不可配置属性删除失败时映射保持（§10.4.4.3）。
    if (detail::toBoolean(result)) {
        uint32_t index = 0;
        if (mappedIndex(d, key, index)) d->mappedNames[index].clear();
    }
    return result;
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
