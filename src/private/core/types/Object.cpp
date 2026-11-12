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
#include "core/types/Array.h"
#include "core/types/Object.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace neo::core {

// —— 运算：走 ToPrimitive（ES2024 §13.15.3 / §13.15.4）——
// 普通对象默认 ToPrimitive 得 "[object Object]"，故 + 为字符串拼接、算术为 NaN。

Value* ObjectType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::addValues(ctx, a, b);
}
Value* ObjectType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '-', a, b);
}
Value* ObjectType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '*', a, b);
}
Value* ObjectType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '/', a, b);
}
Value* ObjectType::toString(IContext* ctx, Value* self) {
    // ToString(object) = ToPrimitive(STRING) 后递归（ES2024 §7.1.14）
    return detail::toStringValue(ctx, self);
}

Value* ObjectType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                        size_t /*argc*/, Value* /*argv*/[]) {
    std::string msg = std::string(self->type()->name()) + " is not a function";
    return detail::typeError(ctx, msg);
}

// 构造调用：普通对象不是构造函数。
Value* ObjectType::construct(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    std::string msg = std::string(self->type()->name()) + " is not a constructor";
    return detail::typeError(ctx, msg);
}

// —— 属性操作 ——
//
// key 是 Value*，按 ToPropertyKey（ES2024 §7.1.19）分流：
//   symbol → symbols 容器（按 SymbolData* 身份）
//   其他   → ToString 后进 properties 容器
// Object/Function 作 key 需 ToPrimitive（未实现），返回 Exception。

namespace {

// 链上节点只可能是具备对象布局的类型（其 Data 派生自 ObjectData），其他类型终止。
// 判定来自 kindHasObjectLayout()（IType 构造时声明），新增 exotic 类型无需改这里。
// 注：不直接用 IType::hasObjectLayout()——成员函数会在 ObjectType 成员里遮蔽同名自由函数。

// 键是否 symbol（symbol 走 symbols 容器）
inline bool isSymbolKey(Value* key) {
    return key->type()->kind() == TypeKind::Symbol;
}

// 键转字符串（非 symbol 键）。走 ToPropertyKey（ES2024 §7.1.19）：
// 对象键先 ToPrimitive(STRING)，故可触发用户 valueOf/toString/@@toPrimitive。
// 成功返回 nullptr 并写入 out；失败返回 Exception/Fatal。
inline Value* keyString(IContext* ctx, Value* key, std::string& out) {
    return detail::propertyKeyString(ctx, key, out);
}

// symbol 键 → symbols 容器的哈希键（SymbolData* 身份）
inline const void* symbolIdentity(Value* key) {
    return static_cast<const void*>(key->data());
}

// 结果是否为错误值（Exception/Fatal），用于访问器调用的透传判断
inline bool isErrorResult(Value* v) {
    TypeKind k = v->type()->kind();
    return k == TypeKind::Exception || k == TypeKind::Fatal;
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
    if (isAccessorDescriptor(*p)) {
        if (p->getter == nullptr) return ctx->createUndefined();
        return callAccessor(ctx, p->getter, receiver);
    }
    return p->value != nullptr ? p->value : ctx->createUndefined();
}

}  // namespace

// —— ToPropertyKey（ES2024 §7.1.19）——
Value* toPropertyKey(IContext* ctx, Value* key, PropertyKey& out) {
    if (isSymbolKey(key)) {
        out.value = key;
        out.isSymbol = true;
        out.symbol = symbolIdentity(key);
        out.name.clear();
        return nullptr;
    }
    std::string k;
    if (Value* err = keyString(ctx, key, k)) return err;
    out.value = ctx->createString(k);
    if (out.value == ctx->oomValue()) return out.value;
    out.isSymbol = false;
    out.symbol = nullptr;
    out.name = std::move(k);
    return nullptr;
}

// —— 属性描述符转换（ES2024 §6.2.6 / §10.1.6.2）——

Value* propertyDescriptorFromValue(IContext* ctx, Value* descObj, Property& out) {
    auto field = [&](const char* name) -> Value* {
        return ctx->getProperty(descObj, ctx->createString(name));
    };

    Value* enumerableV = field("enumerable");
    if (isErrorResult(enumerableV)) return enumerableV;
    Value* configurableV = field("configurable");
    if (isErrorResult(configurableV)) return configurableV;
    bool enumerable = detail::toBoolean(enumerableV);
    bool configurable = detail::toBoolean(configurableV);

    Value* getter = field("get");
    if (isErrorResult(getter)) return getter;
    Value* setter = field("set");
    if (isErrorResult(setter)) return setter;
    Value* value = field("value");
    if (isErrorResult(value)) return value;
    Value* writableV = field("writable");
    if (isErrorResult(writableV)) return writableV;

    bool hasGet = getter->type()->kind() != TypeKind::Undefined;
    bool hasSet = setter->type()->kind() != TypeKind::Undefined;
    bool hasValue = value->type()->kind() != TypeKind::Undefined;
    bool hasWritable = writableV->type()->kind() != TypeKind::Undefined;
    bool isAccessor = hasGet || hasSet;
    bool isData = hasValue || hasWritable;

    if (isAccessor && isData) {
        return detail::typeError(
            ctx, "Invalid property descriptor: cannot be both data and accessor");
    }
    if (isAccessor) {
        auto normalize = [&](Value* v) -> Value* {
            if (v->type()->kind() == TypeKind::Undefined) return nullptr;
            if (!detail::isCallable(v)) {
                return detail::typeError(ctx, "Getter/setter must be a function");
            }
            return v;
        };
        Value* g = normalize(getter);
        if (g != nullptr && isErrorResult(g)) return g;
        Value* s = normalize(setter);
        if (s != nullptr && isErrorResult(s)) return s;
        out = Property{nullptr, false, enumerable, configurable, g, s};
        return nullptr;
    }
    Value* v = hasValue ? value : ctx->createUndefined();
    if (v == ctx->oomValue()) return v;
    out = Property{v, hasWritable ? detail::toBoolean(writableV) : false, enumerable,
                   configurable, nullptr, nullptr};
    return nullptr;
}

Value* propertyDescriptorToValue(IContext* ctx, const Property& p) {
    Value* obj = ctx->createObject();
    if (obj == ctx->oomValue()) return obj;
    auto def = [&](const char* name, Value* v) -> Value* {
        Value* r = ctx->defineProperty(obj, ctx->createString(name), v, true, true, true);
        return isErrorResult(r) ? r : nullptr;
    };
    if (isAccessorDescriptor(p)) {
        Value* g = p.getter != nullptr ? p.getter : ctx->createUndefined();
        Value* s = p.setter != nullptr ? p.setter : ctx->createUndefined();
        if (Value* e = def("get", g)) return e;
        if (Value* e = def("set", s)) return e;
    } else {
        Value* v = p.value != nullptr ? p.value : ctx->createUndefined();
        if (Value* e = def("value", v)) return e;
        if (Value* e = def("writable", ctx->createBoolean(p.writable))) return e;
    }
    if (Value* e = def("enumerable", ctx->createBoolean(p.enumerable))) return e;
    if (Value* e = def("configurable", ctx->createBoolean(p.configurable))) return e;
    return obj;
}

namespace {

bool samePropertyValue(Value* a, Value* b) {
    if (a == b) return true;
    if (a == nullptr || b == nullptr) return false;
    return detail::sameValue(a, b);
}

}  // namespace

bool isCompatiblePropertyDescriptor(const Property& current, const Property& desc) {
    bool currentAccessor = isAccessorDescriptor(current);
    bool descAccessor = isAccessorDescriptor(desc);
    if (currentAccessor != descAccessor) {
        // 数据 ↔ 访问器切换要求当前属性可配置。
        return current.configurable;
    }
    if (!current.configurable) {
        if (desc.configurable) return false;
        if (currentAccessor) {
            if (desc.getter != current.getter || desc.setter != current.setter) return false;
        } else {
            if (desc.writable && !current.writable) return false;
            if (!current.writable &&
                !samePropertyValue(desc.value, current.value)) {
                return false;
            }
        }
    }
    return true;
}

// —— 普通对象内部方法（ES2024 §10.1）——

Value* ordinaryGet(IContext* ctx, Value* obj, const PropertyKey& key, Value* receiver) {
    Property desc;
    bool exists = false;
    if (Value* err = obj->type()->getOwnProperty(ctx, obj, key, exists, desc)) return err;
    if (exists) return readProperty(ctx, &desc, receiver);
    Value* parent = obj->type()->getPrototype(ctx, obj);
    if (isErrorResult(parent)) return parent;
    if (parent == nullptr || !detail::isObjectKind(parent->type()->kind())) {
        return ctx->createUndefined();
    }
    // 经父对象的 [[Get]] 分派：原型链上的 Proxy 在此触发 get trap。
    return parent->type()->getWithReceiver(ctx, parent, key, receiver);
}

Value* ordinarySet(IContext* ctx, Value* obj, const PropertyKey& key, Value* value,
                   Value* receiver) {
    Property own;
    bool hasOwn = false;
    if (Value* err = obj->type()->getOwnProperty(ctx, obj, key, hasOwn, own)) return err;

    if (!hasOwn) {
        Value* parent = obj->type()->getPrototype(ctx, obj);
        if (isErrorResult(parent)) return parent;
        if (parent != nullptr && detail::isObjectKind(parent->type()->kind())) {
            // 继承属性：交给父对象的 [[Set]]（原型链上的 Proxy 在此触发 set trap）。
            return parent->type()->setWithReceiver(ctx, parent, key, value, receiver);
        }
        // 无继承描述符：按默认可写数据属性在 receiver 上创建。
        own = Property{};
        own.writable = true;
        own.enumerable = true;
        own.configurable = true;
    }

    if (isAccessorDescriptor(own)) {
        if (own.setter == nullptr) return ctx->createBoolean(false);
        Value* argv[1] = {value};
        Value* r = callAccessor(ctx, own.setter, receiver, 1, argv);
        return isErrorResult(r) ? r : ctx->createBoolean(true);
    }

    if (!own.writable) return ctx->createBoolean(false);
    if (receiver == nullptr || !detail::isObjectKind(receiver->type()->kind())) {
        return ctx->createBoolean(false);
    }

    // Receiver 已有自有属性：数据属性可写则只更新值（保留属性特性）；
    // 访问器或不可写 → [[Set]] 失败。
    Property existing;
    bool hasExisting = false;
    if (Value* err = receiver->type()->getOwnProperty(ctx, receiver, key, hasExisting,
                                                      existing)) {
        return err;
    }
    if (hasExisting) {
        if (isAccessorDescriptor(existing) || !existing.writable) {
            return ctx->createBoolean(false);
        }
        return receiver->type()->defineProperty(ctx, receiver, key.value, value,
                                                existing.writable, existing.enumerable,
                                                existing.configurable);
    }
    Value* ext = receiver->type()->isExtensible(ctx, receiver);
    if (isErrorResult(ext)) return ext;
    if (!detail::toBoolean(ext)) return ctx->createBoolean(false);
    return receiver->type()->defineProperty(ctx, receiver, key.value, value, true, true,
                                            true);
}

Value* ordinaryHas(IContext* ctx, Value* obj, const PropertyKey& key) {
    Property desc;
    bool exists = false;
    if (Value* err = obj->type()->getOwnProperty(ctx, obj, key, exists, desc)) return err;
    if (exists) return ctx->createBoolean(true);
    Value* parent = obj->type()->getPrototype(ctx, obj);
    if (isErrorResult(parent)) return parent;
    if (parent == nullptr || !detail::isObjectKind(parent->type()->kind())) {
        return ctx->createBoolean(false);
    }
    // 经父对象的 [[HasProperty]] 分派：原型链上的 Proxy 在此触发 has trap。
    return parent->type()->hasProperty(ctx, parent, key.value);
}

Value* ordinaryDelete(IContext* ctx, Value* obj, const PropertyKey& key) {
    Property desc;
    bool exists = false;
    if (Value* err = obj->type()->getOwnProperty(ctx, obj, key, exists, desc)) return err;
    if (!exists) return ctx->createBoolean(true);
    if (!desc.configurable) return ctx->createBoolean(false);
    Value* r = obj->type()->deleteProperty(ctx, obj, key.value);
    if (isErrorResult(r)) return r;
    return r;
}

Value* IType::getWithReceiver(IContext* ctx, Value* self, const PropertyKey& key,
                              Value* receiver) {
    return ordinaryGet(ctx, self, key, receiver);
}

Value* IType::setWithReceiver(IContext* ctx, Value* self, const PropertyKey& key,
                              Value* value, Value* receiver) {
    return ordinarySet(ctx, self, key, value, receiver);
}

Value* IType::isExtensible(IContext* ctx, Value* self) {
    if (!kindHasObjectLayout(kind())) return ctx->createBoolean(false);
    return ctx->createBoolean(static_cast<ObjectData*>(self->data())->extensible);
}

Value* IType::preventExtensions(IContext* ctx, Value* self) {
    if (!kindHasObjectLayout(kind())) {
        return detail::typeError(ctx, "Cannot prevent extensions on non-object");
    }
    static_cast<ObjectData*>(self->data())->extensible = false;
    return ctx->createBoolean(true);
}

Value* ordinaryGetPrototype(IContext* ctx, Value* obj) {
    return obj->type()->getPrototype(ctx, obj);
}

Value* ordinarySetPrototype(IContext* ctx, Value* obj, Value* proto) {
    return obj->type()->setPrototype(ctx, obj, proto);
}

// —— 默认内部方法（OrdinaryGetOwnProperty / OrdinaryOwnPropertyKeys）——

Value* IType::getOwnProperty(IContext* ctx, Value* self, const PropertyKey& key,
                             bool& exists, Property& out) const {
    exists = false;
    if (!kindHasObjectLayout(kind())) return nullptr;
    auto* d = static_cast<ObjectData*>(self->data());

    if (key.isSymbol) {
        auto it = d->symbols.find(key.symbol);
        if (it == d->symbols.end()) return nullptr;
        out = it->second.prop;
        exists = true;
        return nullptr;
    }

    if (hasIndexedElements()) {
        if (key.name == "length" && hasIndexedLength(self)) {
            Value* len = ctx->createNumber(static_cast<double>(indexedLength(self)));
            if (len == ctx->oomValue()) return len;
            // Array.length 可写；String 包装的 length 只读（ES2024 §10.4.3.1）。
            out = Property{len, kind() == TypeKind::Array, false, false, nullptr, nullptr};
            exists = true;
            return nullptr;
        }
        uint32_t index = 0;
        if (parseArrayIndex(key.name, index)) {
            Value* v = indexedElement(ctx, self, index);
            if (v != nullptr) {
                out = Property{v, true, true, true, nullptr, nullptr};
                exists = true;
                return nullptr;
            }
            // 空洞/越界：继续查 properties（String 包装的越界索引可作普通属性定义）。
        }
    }

    auto it = d->properties.find(key.name);
    if (it == d->properties.end()) return nullptr;
    out = it->second;
    exists = true;
    return nullptr;
}

Value* IType::ownKeys(IContext* ctx, Value* self, std::vector<Value*>& out) const {
    if (!kindHasObjectLayout(kind())) return nullptr;
    auto* d = static_cast<ObjectData*>(self->data());

    // 1. 数组索引（升序）：索引型 exotic 的元素 + 普通对象 properties 中的数字串键
    std::vector<uint32_t> indices;
    if (hasIndexedElements()) {
        uint32_t len = indexedLength(self);
        for (uint32_t i = 0; i < len; ++i) {
            if (indexedElement(ctx, self, i) != nullptr) indices.push_back(i);
        }
    }
    for (const std::string& name : d->order) {
        uint32_t index = 0;
        if (parseArrayIndex(name, index)) indices.push_back(index);
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    for (uint32_t index : indices) {
        Value* k = ctx->createString(std::to_string(index));
        if (k == ctx->oomValue()) return k;
        out.push_back(k);
    }

    // 2. 字符串键（插入序，跳过已作为索引输出的键）。
    // 索引型 exotic 的 "length" 是自有属性，且先于其他字符串键（创建最早）。
    if (hasIndexedLength(self)) {
        Value* k = ctx->createString("length");
        if (k == ctx->oomValue()) return k;
        out.push_back(k);
    }
    for (const std::string& name : d->order) {
        uint32_t index = 0;
        if (parseArrayIndex(name, index)) continue;
        if (d->properties.find(name) == d->properties.end()) continue;
        Value* k = ctx->createString(name);
        if (k == ctx->oomValue()) return k;
        out.push_back(k);
    }

    // 3. symbol 键（插入序）
    for (const void* ident : d->symbolOrder) {
        auto it = d->symbols.find(ident);
        if (it == d->symbols.end()) continue;
        out.push_back(it->second.key);
    }
    return nullptr;
}

Value* ObjectType::getProperty(IContext* ctx, Value* self, Value* key) {
    PropertyKey pk;
    if (Value* err = toPropertyKey(ctx, key, pk)) return err;
    return ordinaryGet(ctx, self, pk, self);
}

Value* getPropertyWithReceiver(IContext* ctx, Value* obj, Value* key, Value* receiver) {
    PropertyKey pk;
    if (Value* err = toPropertyKey(ctx, key, pk)) return err;
    // 经 [[Get]] 分派：base 是 Proxy 时触发 get trap。
    return obj->type()->getWithReceiver(ctx, obj, pk, receiver);
}

Value* setPropertyWithReceiver(IContext* ctx, Value* obj, Value* key, Value* value,
                               Value* receiver) {
    PropertyKey pk;
    if (Value* err = toPropertyKey(ctx, key, pk)) return err;
    // 经 [[Set]] 分派：base 是 Proxy 时触发 set trap。
    Value* r = obj->type()->setWithReceiver(ctx, obj, pk, value, receiver);
    if (isErrorResult(r)) return r;
    if (!detail::toBoolean(r)) {
        if (!ctx->hasFlag("strict")) return value;
        std::string name = pk.isSymbol ? detail::valueToString(key) : pk.name;
        return detail::typeError(ctx, "Cannot set property '" + name + "'");
    }
    return value;
}

Value* ObjectType::setProperty(IContext* ctx, Value* self, Value* key, Value* value) {
    // OrdinarySetWithOwnDescriptor（ES2024 §10.1.9.2），逐跳经内部方法分派。
    // 严格性由当前作用域标注决定：sloppy 模式下 [[Set]] 失败静默（表达式值仍为 RHS）。
    PropertyKey pk;
    if (Value* err = toPropertyKey(ctx, key, pk)) return err;
    Value* r = ordinarySet(ctx, self, pk, value, self);
    if (isErrorResult(r)) return r;
    if (!detail::toBoolean(r)) {
        if (!ctx->hasFlag("strict")) return value;
        std::string name = pk.isSymbol ? detail::valueToString(key) : pk.name;
        return detail::typeError(ctx, "Cannot set property '" + name + "'");
    }
    return value;
}

Value* ObjectType::hasProperty(IContext* ctx, Value* self, Value* key) {
    // JS `in` 语义：自有 + 原型链（逐跳经内部方法分派）。
    PropertyKey pk;
    if (Value* err = toPropertyKey(ctx, key, pk)) return err;
    return ordinaryHas(ctx, self, pk);
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
            return detail::typeError(ctx, msg);
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
    std::string k;
    if (Value* err = keyString(ctx, key, k)) return err;
    auto it = d->properties.find(k);
    if (it == d->properties.end()) {
        // 不存在：delete 视为成功（JS 语义）
        return ctx->createBoolean(true);
    }
    if (!it->second.configurable) {
        std::string msg = "Cannot delete property '" + k + "'";
        return detail::typeError(ctx, msg);
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

Value* ObjectType::defineProperty(IContext* ctx, Value* self, Value* key,
                                  Value* value, bool writable, bool enumerable,
                                  bool configurable) {
    auto* d = static_cast<ObjectData*>(self->data());
    Property desc{value, writable, enumerable, configurable, nullptr, nullptr};

    // —— symbol 键 ——
    if (isSymbolKey(key)) {
        const void* ident = symbolIdentity(key);
        auto it = d->symbols.find(ident);
        if (it != d->symbols.end()) {
            if (!isCompatiblePropertyDescriptor(it->second.prop, desc)) {
                return ctx->createBoolean(false);
            }
            it->second.prop = desc;
            return ctx->createBoolean(true);
        }
        if (!d->extensible) return ctx->createBoolean(false);
        d->symbols.emplace(ident, SymbolProperty{key, desc});
        d->symbolOrder.push_back(ident);
        return ctx->createBoolean(true);
    }

    // —— 字符串键 ——
    std::string k;
    if (Value* err = keyString(ctx, key, k)) return err;
    auto it = d->properties.find(k);
    if (it != d->properties.end()) {
        if (!isCompatiblePropertyDescriptor(it->second, desc)) {
            return ctx->createBoolean(false);
        }
        it->second = desc;
        return ctx->createBoolean(true);
    }
    if (!d->extensible) return ctx->createBoolean(false);
    d->properties.emplace(k, desc);
    d->order.push_back(k);
    return ctx->createBoolean(true);
}

Value* ObjectType::defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                                  Value* setter, bool enumerable, bool configurable) {
    // getter/setter 须为 Function 或 null（ES2024 §10.1.6.3 ValidateAndApplyPropertyDescriptor）
    auto validAccessor = [](Value* v) {
        return v == nullptr || v->type()->kind() == TypeKind::Null ||
               detail::isCallable(v);
    };
    if (!validAccessor(getter) || !validAccessor(setter)) {
        return detail::typeError(ctx, "Getter/setter must be a function or null");
    }
    // null 统一存成 nullptr（无访问器）
    Value* g = (getter != nullptr && detail::isCallable(getter)) ? getter : nullptr;
    Value* s = (setter != nullptr && detail::isCallable(setter)) ? setter : nullptr;

    auto* d = static_cast<ObjectData*>(self->data());
    // 访问器描述符：value 为空、writable 无意义（数据属性字段置 false）
    Property acc{nullptr, false, enumerable, configurable, g, s};

    // —— symbol 键 ——
    if (isSymbolKey(key)) {
        const void* ident = symbolIdentity(key);
        auto it = d->symbols.find(ident);
        if (it != d->symbols.end()) {
            if (!isCompatiblePropertyDescriptor(it->second.prop, acc)) {
                return ctx->createBoolean(false);
            }
            it->second.prop = acc;
            return ctx->createBoolean(true);
        }
        if (!d->extensible) return ctx->createBoolean(false);
        d->symbols.emplace(ident, SymbolProperty{key, acc});
        d->symbolOrder.push_back(ident);
        return ctx->createBoolean(true);
    }

    // —— 字符串键 ——
    std::string k;
    if (Value* err = keyString(ctx, key, k)) return err;
    auto it = d->properties.find(k);
    if (it != d->properties.end()) {
        if (!isCompatiblePropertyDescriptor(it->second, acc)) {
            return ctx->createBoolean(false);
        }
        it->second = acc;
        return ctx->createBoolean(true);
    }
    if (!d->extensible) return ctx->createBoolean(false);
    d->properties.emplace(k, acc);
    d->order.push_back(k);
    return ctx->createBoolean(true);
}

// —— 原型链（ADR-018 §1）——

Value* ObjectType::getPrototype(IContext* ctx, Value* self) {
    auto* d = static_cast<ObjectData*>(self->data());
    return d->prototype != nullptr ? d->prototype : ctx->createNull();
}

Value* ObjectType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    // proto 只允许 Object/Function 或 null（undefined 等按 TypeError 处理）
    TypeKind pk = proto->type()->kind();
    if (!kindHasObjectLayout(pk) && pk != TypeKind::Null) {
        return ctx->createBoolean(false);
    }
    // 环检测：从 proto 沿原型链走到头，若遇到 self 则成环。
    // 经内部方法遍历，原型链上的 Proxy 也能正确参与（trap 抛错则透传）。
    std::unordered_set<Value*> seen;
    for (Value* cur = proto; cur != nullptr && kindHasObjectLayout(cur->type()->kind());) {
        if (cur == self) return ctx->createBoolean(false);
        if (!seen.insert(cur).second) break;  // 已有环（内部契约被破坏），终止遍历
        Value* next = cur->type()->getPrototype(ctx, cur);
        if (isErrorResult(next)) return next;
        cur = (next != nullptr && kindHasObjectLayout(next->type()->kind())) ? next : nullptr;
    }
    static_cast<ObjectData*>(self->data())->prototype = (pk == TypeKind::Null) ? nullptr : proto;
    return ctx->createBoolean(true);
}

// —— ObjectData ——

ObjectData::~ObjectData() {
    // 私有名引用计数：每个槽持一份，随 data 销毁归还。
    for (const PrivateSlot& slot : slots) releasePrivateName(slot.name);
}

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
    // 私有字段值（PrivateName 引用计数，不参与 GC 标记）
    for (const PrivateSlot& slot : slots) {
        if (slot.value != nullptr) visitor->visit(slot.value);
    }
}

IData* ObjectData::clone(neo::infra::IAllocator* /*alloc*/) const {
    // 对象是引用语义（ADR-019 §2）：clone 不复制容器，ref++ 后返回 this，
    // 赋值后两个变量指向同一对象，修改互见。
    auto* self = const_cast<ObjectData*>(this);
    self->refInc();
    return self;
}

// —— 自有属性键枚举（ES2024 §10.1.11）——
// 经内部方法 ownKeys 取全部键（Proxy 会触发 ownKeys trap），再按需过滤：
// enumerableOnly 走 getOwnProperty（Proxy 会触发 getOwnPropertyDescriptor trap）。
Value* ownPropertyKeys(IContext* ctx, Value* obj, bool enumerableOnly,
                       bool includeSymbols, std::vector<Value*>& out) {
    if (!detail::isObjectKind(obj->type()->kind())) return nullptr;

    std::vector<Value*> all;
    if (Value* err = obj->type()->ownKeys(ctx, obj, all)) return err;

    for (Value* k : all) {
        bool isSymbol = k->type()->kind() == TypeKind::Symbol;
        if (isSymbol && !includeSymbols) continue;
        if (enumerableOnly) {
            PropertyKey pk;
            if (Value* err = toPropertyKey(ctx, k, pk)) return err;
            Property desc;
            bool exists = false;
            if (Value* err = obj->type()->getOwnProperty(ctx, obj, pk, exists, desc)) {
                return err;
            }
            if (!exists || !desc.enumerable) continue;
        }
        out.push_back(k);
    }
    return nullptr;
}

}  // namespace neo::core
