// Object.cpp - ObjectType 行为方法实现 + ObjectData GC 遍历
//
// 属性操作（ADR-018）：key 是 Value*，按 ToPropertyKey 分流到 properties / symbols。
//   getProperty    —— 沿原型链查找（自有遮蔽原型）
//   setProperty    —— OrdinarySet：自有存在则 writable 校验；否则查原型链后自建
//   hasProperty    —— JS `in` 语义（自有 + 原型链）
//   deleteProperty —— 从 map 与 order 同步移除（O(n)），返回 true / Exception（不可配置）
//   defineProperty —— 完整描述符定义（只操作自有属性）
//
// 关联 ADR: 015（data 持子 value）、018（对象模型）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Value.h"
#include "core/types/Object.h"
#include "core/types/TypeBehavior.h"

#include <string>
#include <unordered_set>

namespace neo::core {

namespace {

// 非对象运算占位错误（对象参与运算需 ToPrimitive，后续阶段）
Value* notImplemented(IContext* ctx, const char* op) {
    std::string msg = std::string("Object.") + op + " is not implemented";
    return ctx->createException(ctx->createString(msg));
}

}  // namespace

// —— 运算：本阶段未实现 ——

Value* ObjectType::add(IContext* ctx, Value* /*a*/, Value* /*b*/) {
    return notImplemented(ctx, "add");
}
Value* ObjectType::sub(IContext* ctx, Value* /*a*/, Value* /*b*/) {
    return notImplemented(ctx, "sub");
}
Value* ObjectType::mul(IContext* ctx, Value* /*a*/, Value* /*b*/) {
    return notImplemented(ctx, "mul");
}
Value* ObjectType::div(IContext* ctx, Value* /*a*/, Value* /*b*/) {
    return notImplemented(ctx, "div");
}
Value* ObjectType::toString(IContext* ctx, Value* /*self*/) {
    return notImplemented(ctx, "toString");
}

Value* ObjectType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                        size_t /*argc*/, Value* /*argv*/[]) {
    std::string msg = std::string(self->type()->name()) + " is not a function";
    return ctx->createException(ctx->createString(msg));
}

// 构造调用：普通对象不是构造函数。
Value* ObjectType::construct(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    std::string msg = std::string(self->type()->name()) + " is not a constructor";
    return ctx->createException(ctx->createString(msg));
}

// —— 属性操作 ——
//
// key 是 Value*，按 ToPropertyKey（ES2024 §7.1.19）分流：
//   symbol → symbols 容器（按 SymbolData* 身份）
//   其他   → ToString 后进 properties 容器
// Object/Function 作 key 需 ToPrimitive（未实现），返回 Exception。

namespace {

// 链上节点只可能是 Object 或 Function（FunctionData 派生自 ObjectData），其他类型终止。
inline bool hasObjectLayout(TypeKind k) {
    return k == TypeKind::Object || k == TypeKind::Function;
}

// 键是否 symbol（symbol 走 symbols 容器）
inline bool isSymbolKey(Value* key) {
    return key->type()->kind() == TypeKind::Symbol;
}

// 键转字符串（非 symbol 键）
inline std::string keyString(Value* key) {
    return detail::keyToString(key);
}

// symbol 键 → symbols 容器的哈希键（SymbolData* 身份）
inline const void* symbolIdentity(Value* key) {
    return static_cast<const void*>(key->data());
}

// 沿原型链查找字符串键属性（含 self 自身）。返回命中的 Property，未找到 nullptr。
Property* findInChain(Value* self, const std::string& key) {
    for (Value* cur = self; cur != nullptr;) {
        if (!hasObjectLayout(cur->type()->kind())) return nullptr;
        auto* d = static_cast<ObjectData*>(cur->data());
        auto it = d->properties.find(key);
        if (it != d->properties.end()) return &it->second;
        cur = d->prototype;
    }
    return nullptr;
}

// 沿原型链查找 symbol 键属性（含 self 自身）。返回命中的 Property，未找到 nullptr。
Property* findSymbolInChain(Value* self, const void* ident) {
    for (Value* cur = self; cur != nullptr;) {
        if (!hasObjectLayout(cur->type()->kind())) return nullptr;
        auto* d = static_cast<ObjectData*>(cur->data());
        auto it = d->symbols.find(ident);
        if (it != d->symbols.end()) return &it->second.prop;
        cur = d->prototype;
    }
    return nullptr;
}

// 结果是否为错误值（Exception/Fatal），用于访问器调用的透传判断
inline bool isErrorResult(Value* v) {
    TypeKind k = v->type()->kind();
    return k == TypeKind::Exception || k == TypeKind::Fatal;
}

// 是否访问器属性（getter/setter 任一非空）
inline bool isAccessor(const Property& p) {
    return p.getter != nullptr || p.setter != nullptr;
}

// 调用访问器函数：以 receiver 为 this。
// getter 无参数；setter 接收一个参数（被赋的值）。
// 返回函数结果；Exception/Fatal 由调用方透传（异常即值，ADR-006/017）。
Value* callAccessor(IContext* ctx, Value* fn, Value* receiver, size_t argc = 0,
                    Value* argv[] = nullptr) {
    return ctx->call(fn, receiver, argc, argv);
}

// 按属性描述符读取：数据属性返回值，访问器属性以 receiver 为 this 调 getter。
// getter 为空 → undefined；getter 抛错 → 透传。
Value* readProperty(IContext* ctx, Property* p, Value* receiver) {
    if (p == nullptr) return ctx->createUndefined();
    if (isAccessor(*p)) {
        if (p->getter == nullptr) return ctx->createUndefined();
        return callAccessor(ctx, p->getter, receiver);
    }
    return p->value != nullptr ? p->value : ctx->createUndefined();
}

}  // namespace

Value* ObjectType::getProperty(IContext* ctx, Value* self, Value* key) {
    // 沿原型链查找（JS 读取语义）；命中访问器则以 self（receiver）为 this 调 getter
    Property* p = isSymbolKey(key) ? findSymbolInChain(self, symbolIdentity(key))
                                   : findInChain(self, keyString(key));
    return readProperty(ctx, p, self);
}

Value* ObjectType::setProperty(IContext* ctx, Value* self, Value* key, Value* value) {
    auto* d = static_cast<ObjectData*>(self->data());
    // OrdinarySetWithOwnDescriptor（ES2024 §10.1.9.2）：
    //   1. 自有属性存在 → 按其描述符处理（访问器调 setter；数据属性查 writable）
    //   2. 自有不存在 → 沿原型链找继承描述符：
    //        - 访问器有 setter → 以 receiver(self) 为 this 调用
    //        - 访问器无 setter → 严格模式报错
    //        - 数据属性不可写 → 严格模式报错
    //        - 其余 → 在自身上新建数据属性
    // 本阶段 receiver 恒为 self（无 Reflect.set 的显式 receiver 参数）。

    // —— symbol 键 ——
    if (isSymbolKey(key)) {
        const void* ident = symbolIdentity(key);
        auto it = d->symbols.find(ident);
        if (it != d->symbols.end()) {
            Property& own = it->second.prop;
            if (isAccessor(own)) {
                if (own.setter == nullptr) {
                    std::string msg = "Cannot set property '" + detail::valueToString(key) +
                                      "' which has only a getter";
                    return ctx->createException(ctx->createString(msg));
                }
                Value* argv[1] = {value};
                Value* r = callAccessor(ctx, own.setter, self, 1, argv);
                return isErrorResult(r) ? r : value;
            }
            if (!own.writable) {
                std::string msg = "Cannot assign to read only property '" +
                                  detail::valueToString(key) + "'";
                return ctx->createException(ctx->createString(msg));
            }
            own.value = value;
            return value;
        }
        Property* inherited = findSymbolInChain(d->prototype, ident);
        if (inherited != nullptr) {
            if (isAccessor(*inherited)) {
                if (inherited->setter == nullptr) {
                    std::string msg = "Cannot set property '" + detail::valueToString(key) +
                                      "' which has only a getter";
                    return ctx->createException(ctx->createString(msg));
                }
                Value* argv[1] = {value};
                Value* r = callAccessor(ctx, inherited->setter, self, 1, argv);
                return isErrorResult(r) ? r : value;
            }
            if (!inherited->writable) {
                std::string msg = "Cannot assign to read only property '" +
                                  detail::valueToString(key) + "'";
                return ctx->createException(ctx->createString(msg));
            }
        }
        d->symbols.emplace(ident, SymbolProperty{key, Property{value, true, true, true, nullptr, nullptr}});
        d->symbolOrder.push_back(ident);
        return value;
    }

    // —— 字符串键 ——
    std::string k = keyString(key);
    auto it = d->properties.find(k);
    if (it != d->properties.end()) {
        Property& own = it->second;
        if (isAccessor(own)) {
            if (own.setter == nullptr) {
                std::string msg = "Cannot set property '" + k + "' which has only a getter";
                return ctx->createException(ctx->createString(msg));
            }
            Value* argv[1] = {value};
                Value* r = callAccessor(ctx, own.setter, self, 1, argv);
            return isErrorResult(r) ? r : value;
        }
        if (!own.writable) {
            std::string msg = "Cannot assign to read only property '" + k + "'";
            return ctx->createException(ctx->createString(msg));
        }
        own.value = value;
        return value;
    }
    Property* inherited = findInChain(d->prototype, k);
    if (inherited != nullptr) {
        if (isAccessor(*inherited)) {
            if (inherited->setter == nullptr) {
                std::string msg = "Cannot set property '" + k + "' which has only a getter";
                return ctx->createException(ctx->createString(msg));
            }
            Value* argv[1] = {value};
                Value* r = callAccessor(ctx, inherited->setter, self, 1, argv);
            return isErrorResult(r) ? r : value;
        }
        if (!inherited->writable) {
            std::string msg = "Cannot assign to read only property '" + k + "'";
            return ctx->createException(ctx->createString(msg));
        }
    }
    d->properties.emplace(k, Property{value, true, true, true, nullptr, nullptr});
    d->order.push_back(k);
    return value;
}

bool ObjectType::hasProperty(IContext* /*ctx*/, Value* self, Value* key) {
    // JS `in` 语义：自有 + 原型链
    if (isSymbolKey(key)) return findSymbolInChain(self, symbolIdentity(key)) != nullptr;
    return findInChain(self, keyString(key)) != nullptr;
}

Value* ObjectType::deleteProperty(IContext* ctx, Value* self, Value* key) {
    auto* d = static_cast<ObjectData*>(self->data());

    // —— symbol 键 ——
    if (isSymbolKey(key)) {
        const void* ident = symbolIdentity(key);
        auto it = d->symbols.find(ident);
        if (it == d->symbols.end()) return ctx->createBoolean(true);
        if (!it->second.prop.configurable) {
            std::string msg = "Cannot delete property '" + detail::valueToString(key) + "'";
            return ctx->createException(ctx->createString(msg));
        }
        d->symbols.erase(it);
        for (auto oit = d->symbolOrder.begin(); oit != d->symbolOrder.end(); ++oit) {
            if (*oit == ident) {
                d->symbolOrder.erase(oit);
                break;
            }
        }
        return ctx->createBoolean(true);
    }

    // —— 字符串键 ——
    std::string k = keyString(key);
    auto it = d->properties.find(k);
    if (it == d->properties.end()) {
        // 不存在：delete 视为成功（JS 语义）
        return ctx->createBoolean(true);
    }
    if (!it->second.configurable) {
        std::string msg = "Cannot delete property '" + k + "'";
        return ctx->createException(ctx->createString(msg));
    }
    // 从 map 与 order 同步移除（O(n)，ADR-018 §4）
    d->properties.erase(it);
    for (auto oit = d->order.begin(); oit != d->order.end(); ++oit) {
        if (*oit == k) {
            d->order.erase(oit);
            break;
        }
    }
    return ctx->createBoolean(true);
}

Value* ObjectType::defineProperty(IContext* /*ctx*/, Value* self, Value* key,
                                  Value* value, bool writable, bool enumerable,
                                  bool configurable) {
    auto* d = static_cast<ObjectData*>(self->data());

    // —— symbol 键 ——
    if (isSymbolKey(key)) {
        const void* ident = symbolIdentity(key);
        auto it = d->symbols.find(ident);
        if (it != d->symbols.end()) {
            it->second.prop = Property{value, writable, enumerable, configurable, nullptr, nullptr};
            return value;
        }
        d->symbols.emplace(ident, SymbolProperty{
            key, Property{value, writable, enumerable, configurable, nullptr, nullptr}});
        d->symbolOrder.push_back(ident);
        return value;
    }

    // —— 字符串键 ——
    std::string k = keyString(key);
    auto it = d->properties.find(k);
    if (it != d->properties.end()) {
        // 已存在：整体替换描述符（数据属性覆盖访问器时清空 getter/setter）
        it->second = Property{value, writable, enumerable, configurable, nullptr, nullptr};
        return value;
    }
    d->properties.emplace(k, Property{value, writable, enumerable, configurable, nullptr, nullptr});
    d->order.push_back(k);
    return value;
}

Value* ObjectType::defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                                  Value* setter, bool enumerable, bool configurable) {
    // getter/setter 须为 Function 或 null（ES2024 §10.1.6.3 ValidateAndApplyPropertyDescriptor）
    auto validAccessor = [](Value* v) {
        return v == nullptr || v->type()->kind() == TypeKind::Null ||
               v->type()->kind() == TypeKind::Function;
    };
    if (!validAccessor(getter) || !validAccessor(setter)) {
        return ctx->createException(
            ctx->createString("Getter/setter must be a function or null"));
    }
    // null 统一存成 nullptr（无访问器）
    Value* g = (getter != nullptr && getter->type()->kind() == TypeKind::Function) ? getter : nullptr;
    Value* s = (setter != nullptr && setter->type()->kind() == TypeKind::Function) ? setter : nullptr;

    auto* d = static_cast<ObjectData*>(self->data());
    // 访问器描述符：value 为空、writable 无意义（数据属性字段置 false）
    Property acc{nullptr, false, enumerable, configurable, g, s};

    // —— symbol 键 ——
    if (isSymbolKey(key)) {
        const void* ident = symbolIdentity(key);
        auto it = d->symbols.find(ident);
        if (it != d->symbols.end()) {
            it->second.prop = acc;
            return self;
        }
        d->symbols.emplace(ident, SymbolProperty{key, acc});
        d->symbolOrder.push_back(ident);
        return self;
    }

    // —— 字符串键 ——
    std::string k = keyString(key);
    auto it = d->properties.find(k);
    if (it != d->properties.end()) {
        it->second = acc;
        return self;
    }
    d->properties.emplace(k, acc);
    d->order.push_back(k);
    return self;
}

// —— 原型链（ADR-018 §1）——

Value* ObjectType::getPrototype(IContext* ctx, Value* self) {
    auto* d = static_cast<ObjectData*>(self->data());
    return d->prototype != nullptr ? d->prototype : ctx->createNull();
}

Value* ObjectType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    // proto 只允许 Object/Function 或 null（undefined 等按 TypeError 处理）
    TypeKind pk = proto->type()->kind();
    if (!hasObjectLayout(pk) && pk != TypeKind::Null) {
        std::string msg = "Object prototype may only be an Object or null";
        return ctx->createException(ctx->createString(msg));
    }
    // 环检测：从 proto 沿原型链走到头，若遇到 self 则成环
    std::unordered_set<Value*> seen;
    for (Value* cur = proto; cur != nullptr && hasObjectLayout(cur->type()->kind());) {
        if (cur == self) {
            std::string msg = "Cyclic __proto__ value";
            return ctx->createException(ctx->createString(msg));
        }
        if (!seen.insert(cur).second) break;  // 已有环（内部契约被破坏），终止遍历
        cur = static_cast<ObjectData*>(cur->data())->prototype;
    }
    static_cast<ObjectData*>(self->data())->prototype = (pk == TypeKind::Null) ? nullptr : proto;
    return self;
}

// —— ObjectData ——

void ObjectData::enumerateChildren(IGCVisitor* visitor) {
    // 原型链
    if (prototype != nullptr) visitor->visit(prototype);
    // 所有 property 的 value/getter/setter
    for (auto& [key, prop] : properties) {
        (void)key;
        if (prop.value != nullptr) visitor->visit(prop.value);
        if (prop.getter != nullptr) visitor->visit(prop.getter);
        if (prop.setter != nullptr) visitor->visit(prop.setter);
    }
    // symbol 属性：key Value 本身需保活（否则 symbol 被回收后键悬垂），
    // 以及描述符的 value/getter/setter
    for (auto& [ident, sp] : symbols) {
        (void)ident;
        if (sp.key != nullptr) visitor->visit(sp.key);
        if (sp.prop.value != nullptr) visitor->visit(sp.prop.value);
        if (sp.prop.getter != nullptr) visitor->visit(sp.prop.getter);
        if (sp.prop.setter != nullptr) visitor->visit(sp.prop.setter);
    }
}

IData* ObjectData::clone(neo::infra::IAllocator* /*alloc*/) const {
    // 对象是引用语义（ADR-019 §2）：clone 不复制容器，ref++ 后返回 this，
    // 赋值后两个变量指向同一对象，修改互见。
    auto* self = const_cast<ObjectData*>(this);
    self->refInc();
    return self;
}

}  // namespace neo::core
