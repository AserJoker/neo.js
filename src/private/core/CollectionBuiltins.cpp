// CollectionBuiltins.cpp - Map/Set/WeakMap/WeakSet（ES2024 §24）
//
// 设计要点：
//   - 四种集合共用 CollectionData（有序条目列表），差异由 kind 与原型区分；
//   - 键查找使用 SameValueZero（+0 === -0，NaN === NaN）；
//   - 集合迭代器按下标 live 遍历，删除的条目自然被跳过；
//   - WeakMap/WeakSet 目前以强引用保存键（未接入弱引用 GC），
//     仅实现可观察的 JS 语义。
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、034（原型-构造函数）

#include "core/CollectionBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Collection.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <cmath>
#include <string>
#include <vector>

namespace neo::core {

namespace {

// —— 键比较与查找 ——

bool sameValueZero(Value* a, Value* b) {
    if (a == b) return true;
    if (a->type()->kind() == TypeKind::Number && b->type()->kind() == TypeKind::Number) {
        const double x = static_cast<NumberData*>(a->data())->value;
        const double y = static_cast<NumberData*>(b->data())->value;
        return x == y || (std::isnan(x) && std::isnan(y));
    }
    return detail::strictEquals(a, b);
}

size_t findEntry(const CollectionData* d, Value* key) {
    for (size_t i = 0; i < d->entries.size(); ++i) {
        if (sameValueZero(d->entries[i].key, key)) return i;
    }
    return static_cast<size_t>(-1);
}

// 成功返回 nullptr 并写入 out；接收者不匹配返回 TypeError。
Value* requireCollection(IContext* ctx, Value* self, CollectionKind kind,
                         const char* method, CollectionData*& out) {
    out = asCollection(self);
    if (out == nullptr || out->kind != kind) {
        return detail::typeError(ctx, std::string("Collection.prototype.") + method +
                                          " called on incompatible receiver");
    }
    return nullptr;
}

Value* createIterResultObject(IContext* ctx, Value* value, bool done);

bool canBeHeldWeakly(Value* v) {
    TypeKind k = v->type()->kind();
    return detail::isObjectKind(k) || k == TypeKind::Symbol;
}

void setEntry(CollectionData* d, Value* key, Value* value) {
    const size_t index = findEntry(d, key);
    if (index != static_cast<size_t>(-1)) {
        d->entries[index].value = value;
        return;
    }
    d->entries.push_back(CollectionEntry{key, value});
}

void addValue(CollectionData* d, Value* value) {
    if (findEntry(d, value) == static_cast<size_t>(-1)) {
        d->entries.push_back(CollectionEntry{value, value});
    }
}

// —— 构造函数 ——

Value* collectionCtorCall(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                          Value* /*argv*/[]) {
    return detail::typeError(ctx, "Constructor requires 'new'");
}

// 把 iterable 的条目读成 Value* 列表（Map/Set 构造共用）。
Value* collectEntries(IContext* ctx, Value* iterable, std::vector<Value*>& out) {
    if (iterable->type()->kind() == TypeKind::Undefined ||
        iterable->type()->kind() == TypeKind::Null) {
        return nullptr;
    }
    return detail::iterableToList(ctx, iterable, out);
}

Value* mapConstruct(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* map = ctx->createCollection(CollectionKind::Map);
    if (detail::isErrorValue(map)) return map;
    if (argc == 0) return map;
    std::vector<Value*> entries;
    if (Value* err = collectEntries(ctx, argv[0], entries)) return err;
    for (Value* entry : entries) {
        if (!detail::isObjectKind(entry->type()->kind())) {
            return detail::typeError(ctx, "Iterator value is not an entry object");
        }
        Value* key = ctx->getProperty(entry, "0");
        if (detail::isErrorValue(key)) return key;
        Value* value = ctx->getProperty(entry, "1");
        if (detail::isErrorValue(value)) return value;
        setEntry(asCollection(map), key, value);
    }
    return map;
}

Value* setConstruct(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* set = ctx->createCollection(CollectionKind::Set);
    if (detail::isErrorValue(set)) return set;
    if (argc == 0) return set;
    std::vector<Value*> values;
    if (Value* err = collectEntries(ctx, argv[0], values)) return err;
    for (Value* value : values) addValue(asCollection(set), value);
    return set;
}

Value* weakMapConstruct(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* map = ctx->createCollection(CollectionKind::WeakMap);
    if (detail::isErrorValue(map)) return map;
    if (argc == 0) return map;
    std::vector<Value*> entries;
    if (Value* err = collectEntries(ctx, argv[0], entries)) return err;
    for (Value* entry : entries) {
        if (!detail::isObjectKind(entry->type()->kind())) {
            return detail::typeError(ctx, "Iterator value is not an entry object");
        }
        Value* key = ctx->getProperty(entry, "0");
        if (detail::isErrorValue(key)) return key;
        if (!canBeHeldWeakly(key)) {
            return detail::typeError(ctx, "Invalid value used as weak map key");
        }
        Value* value = ctx->getProperty(entry, "1");
        if (detail::isErrorValue(value)) return value;
        setEntry(asCollection(map), key, value);
    }
    return map;
}

Value* weakSetConstruct(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* set = ctx->createCollection(CollectionKind::WeakSet);
    if (detail::isErrorValue(set)) return set;
    if (argc == 0) return set;
    std::vector<Value*> values;
    if (Value* err = collectEntries(ctx, argv[0], values)) return err;
    for (Value* value : values) {
        if (!canBeHeldWeakly(value)) {
            return detail::typeError(ctx, "Invalid value used in weak set");
        }
        addValue(asCollection(set), value);
    }
    return set;
}

// —— Map.prototype ——

Value* mapGet(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::Map, "get", d))
        return err;
    if (argc == 0) return ctx->createUndefined();
    const size_t index = findEntry(d, argv[0]);
    return index == static_cast<size_t>(-1) ? ctx->createUndefined()
                                            : d->entries[index].value;
}

Value* mapSet(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::Map, "set", d))
        return err;
    Value* key = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* value = argc > 1 ? argv[1] : ctx->createUndefined();
    setEntry(d, key, value);
    return self;
}

Value* mapHas(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::Map, "has", d))
        return err;
    return ctx->createBoolean(argc > 0 && findEntry(d, argv[0]) != static_cast<size_t>(-1));
}

Value* mapDelete(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::Map, "delete", d))
        return err;
    if (argc == 0) return ctx->createBoolean(false);
    const size_t index = findEntry(d, argv[0]);
    if (index == static_cast<size_t>(-1)) return ctx->createBoolean(false);
    d->entries.erase(d->entries.begin() + static_cast<std::ptrdiff_t>(index));
    return ctx->createBoolean(true);
}

Value* collectionClear(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    CollectionData* d = asCollection(self);
    if (d == nullptr) {
        return detail::typeError(ctx, "clear called on incompatible receiver");
    }
    d->entries.clear();
    return ctx->createUndefined();
}

Value* mapSizeGet(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::Map, "size", d))
        return err;
    return ctx->createNumber(static_cast<double>(d->entries.size()));
}

Value* mapForEach(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::Map, "forEach", d))
        return err;
    Value* callback = argc > 0 ? argv[0] : ctx->createUndefined();
    if (!detail::isCallable(callback)) {
        return detail::typeError(ctx, "Map.prototype.forEach callback must be callable");
    }
    Value* thisArg = argc > 1 ? argv[1] : ctx->createUndefined();
    // live 遍历：新增条目会被访问，删除的条目自然跳过。
    for (size_t i = 0; i < d->entries.size(); ++i) {
        Value* args[3] = {d->entries[i].value, d->entries[i].key, self};
        Value* result = ctx->call(callback, thisArg, 3, args);
        if (detail::isErrorValue(result)) return result;
    }
    return ctx->createUndefined();
}

// —— Set.prototype ——

Value* setAdd(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::Set, "add", d))
        return err;
    addValue(d, argc > 0 ? argv[0] : ctx->createUndefined());
    return self;
}

Value* setHas(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::Set, "has", d))
        return err;
    return ctx->createBoolean(argc > 0 && findEntry(d, argv[0]) != static_cast<size_t>(-1));
}

Value* setDelete(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::Set, "delete", d))
        return err;
    if (argc == 0) return ctx->createBoolean(false);
    const size_t index = findEntry(d, argv[0]);
    if (index == static_cast<size_t>(-1)) return ctx->createBoolean(false);
    d->entries.erase(d->entries.begin() + static_cast<std::ptrdiff_t>(index));
    return ctx->createBoolean(true);
}

Value* setSizeGet(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::Set, "size", d))
        return err;
    return ctx->createNumber(static_cast<double>(d->entries.size()));
}

Value* setForEach(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::Set, "forEach", d))
        return err;
    Value* callback = argc > 0 ? argv[0] : ctx->createUndefined();
    if (!detail::isCallable(callback)) {
        return detail::typeError(ctx, "Set.prototype.forEach callback must be callable");
    }
    Value* thisArg = argc > 1 ? argv[1] : ctx->createUndefined();
    for (size_t i = 0; i < d->entries.size(); ++i) {
        Value* value = d->entries[i].key;
        Value* args[3] = {value, value, self};
        Value* result = ctx->call(callback, thisArg, 3, args);
        if (detail::isErrorValue(result)) return result;
    }
    return ctx->createUndefined();
}

// —— WeakMap / WeakSet ——

Value* weakMapGet(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::WeakMap, "get", d))
        return err;
    if (argc == 0) return ctx->createUndefined();
    const size_t index = findEntry(d, argv[0]);
    return index == static_cast<size_t>(-1) ? ctx->createUndefined()
                                            : d->entries[index].value;
}

Value* weakMapSet(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::WeakMap, "set", d))
        return err;
    Value* key = argc > 0 ? argv[0] : ctx->createUndefined();
    if (!canBeHeldWeakly(key)) {
        return detail::typeError(ctx, "Invalid value used as weak map key");
    }
    Value* value = argc > 1 ? argv[1] : ctx->createUndefined();
    setEntry(d, key, value);
    return self;
}

Value* weakMapHas(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::WeakMap, "has", d))
        return err;
    return ctx->createBoolean(argc > 0 && findEntry(d, argv[0]) != static_cast<size_t>(-1));
}

Value* weakMapDelete(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::WeakMap, "delete", d))
        return err;
    if (argc == 0) return ctx->createBoolean(false);
    const size_t index = findEntry(d, argv[0]);
    if (index == static_cast<size_t>(-1)) return ctx->createBoolean(false);
    d->entries.erase(d->entries.begin() + static_cast<std::ptrdiff_t>(index));
    return ctx->createBoolean(true);
}

Value* weakSetAdd(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::WeakSet, "add", d))
        return err;
    Value* value = argc > 0 ? argv[0] : ctx->createUndefined();
    if (!canBeHeldWeakly(value)) {
        return detail::typeError(ctx, "Invalid value used in weak set");
    }
    addValue(d, value);
    return self;
}

Value* weakSetHas(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::WeakSet, "has", d))
        return err;
    return ctx->createBoolean(argc > 0 && findEntry(d, argv[0]) != static_cast<size_t>(-1));
}

Value* weakSetDelete(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::WeakSet, "delete", d))
        return err;
    if (argc == 0) return ctx->createBoolean(false);
    const size_t index = findEntry(d, argv[0]);
    if (index == static_cast<size_t>(-1)) return ctx->createBoolean(false);
    d->entries.erase(d->entries.begin() + static_cast<std::ptrdiff_t>(index));
    return ctx->createBoolean(true);
}

// —— 迭代器 ——

Value* collectionIteratorMethod(IContext* ctx, Value* self, CollectionKind kind,
                                CollectionIterationKind iterKind) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, kind, "iterator", d)) return err;
    (void)d;
    return ctx->createCollectionIterator(self, iterKind);
}

Value* mapKeys(IContext* ctx, Value* self, size_t, Value*[]) {
    return collectionIteratorMethod(ctx, self, CollectionKind::Map,
                                    CollectionIterationKind::Key);
}
Value* mapValues(IContext* ctx, Value* self, size_t, Value*[]) {
    return collectionIteratorMethod(ctx, self, CollectionKind::Map,
                                    CollectionIterationKind::Value);
}
Value* mapEntries(IContext* ctx, Value* self, size_t, Value*[]) {
    return collectionIteratorMethod(ctx, self, CollectionKind::Map,
                                    CollectionIterationKind::KeyAndValue);
}
Value* setValues(IContext* ctx, Value* self, size_t, Value*[]) {
    return collectionIteratorMethod(ctx, self, CollectionKind::Set,
                                    CollectionIterationKind::Value);
}
Value* setEntries(IContext* ctx, Value* self, size_t, Value*[]) {
    return collectionIteratorMethod(ctx, self, CollectionKind::Set,
                                    CollectionIterationKind::KeyAndValue);
}

Value* collectionIteratorNext(IContext* ctx, Value* self, size_t /*argc*/,
                              Value* /*argv*/[]) {
    CollectionIteratorData* it = asCollectionIterator(self);
    if (it == nullptr) {
        return detail::typeError(ctx, "next method called on incompatible receiver");
    }
    if (it->collection == nullptr) {
        return createIterResultObject(ctx, ctx->createUndefined(), true);
    }
    CollectionData* d = asCollection(it->collection);
    if (d == nullptr || it->index >= d->entries.size()) {
        it->collection = nullptr;
        return createIterResultObject(ctx, ctx->createUndefined(), true);
    }
    const CollectionEntry& entry = d->entries[it->index];
    ++it->index;
    switch (it->kind) {
        case CollectionIterationKind::Key:
            return createIterResultObject(ctx, entry.key, false);
        case CollectionIterationKind::Value:
            return createIterResultObject(ctx, entry.value, false);
        case CollectionIterationKind::KeyAndValue: {
            Value* pair = ctx->createArray();
            if (detail::isErrorValue(pair)) return pair;
            auto* pd = static_cast<ArrayData*>(pair->data());
            pd->setElement(0, entry.key);
            pd->setElement(1, entry.value);
            return createIterResultObject(ctx, pair, false);
        }
    }
    return createIterResultObject(ctx, ctx->createUndefined(), true);
}

// —— 注册辅助 ——

Value* createIterResultObject(IContext* ctx, Value* value, bool done) {
    Value* result = ctx->createObject();
    if (detail::isErrorValue(result)) return result;
    Value* r = ctx->defineProperty(result, "value", value, true, true, true);
    if (detail::isErrorValue(r)) return r;
    r = ctx->defineProperty(result, "done", ctx->createBoolean(done), true, true, true);
    return detail::isErrorValue(r) ? r : result;
}

bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

bool defineSymbolMethod(IContext* ctx, Value* target, WellKnownSymbol which,
                        const char* name, NativeFn fn, size_t minArgc) {
    Value* f = ctx->createNativeFunction(fn, minArgc);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    return ctx->defineProperty(target, ctx->getWellKnownSymbol(which), f, true, false,
                               true)
               ->type()->kind() != TypeKind::Exception;
}

bool defineGetter(IContext* ctx, Value* target, const char* name, NativeFn getter) {
    Value* f = ctx->createNativeFunction(getter, 0);
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, std::string("get ") + name);
    return ctx->defineAccessor(target, name, f, nullptr, false, true)
               ->type()->kind() != TypeKind::Exception;
}

bool defineTag(IContext* ctx, Value* target, const char* tag) {
    return ctx->defineProperty(target, ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                               ctx->createString(tag), false, false, true)
               ->type()->kind() != TypeKind::Exception;
}

// 建立单个集合构造函数：prototype + constructor + 静态/原型方法挂接后注册到 global。
bool registerCtor(IContext* ctx, const char* name, NativeFn construct, Value* proto) {
    Value* fn = ctx->createNativeFunction(collectionCtorCall, 0);
    if (fn == ctx->oomValue()) return false;
    ctx->setFunctionName(fn, name);
    ctx->setFunctionConstructor(fn, construct);
    if (ctx->defineProperty(fn, "prototype", proto, false, false, false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (ctx->defineProperty(proto, "constructor", fn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    ctx->defineGlobal(name, fn, true, false, true);
    return true;
}

}  // namespace

bool registerCollectionBuiltins(IContext* ctx) {
    // —— 1. 四个集合原型（原型 = Object.prototype）——
    Value* mapProto = ctx->createObject();
    Value* setProto = ctx->createObject();
    Value* weakMapProto = ctx->createObject();
    Value* weakSetProto = ctx->createObject();
    if (mapProto == ctx->oomValue() || setProto == ctx->oomValue() ||
        weakMapProto == ctx->oomValue() || weakSetProto == ctx->oomValue()) {
        return false;
    }
    for (Value* proto : {mapProto, setProto, weakMapProto, weakSetProto}) {
        if (ctx->setPrototype(proto, ctx->objectPrototype())->type()->kind() ==
            TypeKind::Exception) {
            return false;
        }
    }

    // —— 2. 两个迭代器原型（原型 = %Iterator.prototype%）——
    Value* mapIterProto = ctx->createObject();
    Value* setIterProto = ctx->createObject();
    if (mapIterProto == ctx->oomValue() || setIterProto == ctx->oomValue()) return false;
    for (Value* proto : {mapIterProto, setIterProto}) {
        if (ctx->setPrototype(proto, ctx->iteratorPrototype())->type()->kind() ==
            TypeKind::Exception) {
            return false;
        }
    }

    // —— 3. 原型就绪 ——
    ctx->setCollectionPrototypes(mapProto, setProto, weakMapProto, weakSetProto,
                                 mapIterProto, setIterProto);

    // —— 4. 迭代器原型成员 ——
    if (!defineMethod(ctx, mapIterProto, "next", collectionIteratorNext, 0)) return false;
    if (!defineTag(ctx, mapIterProto, "Map Iterator")) return false;
    if (!defineMethod(ctx, setIterProto, "next", collectionIteratorNext, 0)) return false;
    if (!defineTag(ctx, setIterProto, "Set Iterator")) return false;

    // —— 5. Map.prototype ——
    if (!defineMethod(ctx, mapProto, "get", mapGet, 1)) return false;
    if (!defineMethod(ctx, mapProto, "set", mapSet, 2)) return false;
    if (!defineMethod(ctx, mapProto, "has", mapHas, 1)) return false;
    if (!defineMethod(ctx, mapProto, "delete", mapDelete, 1)) return false;
    if (!defineMethod(ctx, mapProto, "clear", collectionClear, 0)) return false;
    if (!defineMethod(ctx, mapProto, "forEach", mapForEach, 1)) return false;
    if (!defineMethod(ctx, mapProto, "keys", mapKeys, 0)) return false;
    if (!defineMethod(ctx, mapProto, "values", mapValues, 0)) return false;
    if (!defineMethod(ctx, mapProto, "entries", mapEntries, 0)) return false;
    if (!defineGetter(ctx, mapProto, "size", mapSizeGet)) return false;
    if (!defineTag(ctx, mapProto, "Map")) return false;
    if (!defineSymbolMethod(ctx, mapProto, WellKnownSymbol::Iterator,
                            "[Symbol.iterator]", mapEntries, 0)) {
        return false;
    }

    // —— 6. Set.prototype ——
    if (!defineMethod(ctx, setProto, "add", setAdd, 1)) return false;
    if (!defineMethod(ctx, setProto, "has", setHas, 1)) return false;
    if (!defineMethod(ctx, setProto, "delete", setDelete, 1)) return false;
    if (!defineMethod(ctx, setProto, "clear", collectionClear, 0)) return false;
    if (!defineMethod(ctx, setProto, "forEach", setForEach, 1)) return false;
    // keys 与 values 是同一个函数对象（§24.2.3.8 / §24.2.3.10）
    if (!defineMethod(ctx, setProto, "values", setValues, 0)) return false;
    Value* setValuesFn = ctx->getProperty(setProto, "values");
    if (detail::isErrorValue(setValuesFn)) return false;
    if (ctx->defineProperty(setProto, "keys", setValuesFn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineMethod(ctx, setProto, "entries", setEntries, 0)) return false;
    if (!defineGetter(ctx, setProto, "size", setSizeGet)) return false;
    if (!defineTag(ctx, setProto, "Set")) return false;
    if (!defineSymbolMethod(ctx, setProto, WellKnownSymbol::Iterator,
                            "[Symbol.iterator]", setValues, 0)) {
        return false;
    }

    // —— 7. WeakMap.prototype ——
    if (!defineMethod(ctx, weakMapProto, "get", weakMapGet, 1)) return false;
    if (!defineMethod(ctx, weakMapProto, "set", weakMapSet, 2)) return false;
    if (!defineMethod(ctx, weakMapProto, "has", weakMapHas, 1)) return false;
    if (!defineMethod(ctx, weakMapProto, "delete", weakMapDelete, 1)) return false;
    if (!defineTag(ctx, weakMapProto, "WeakMap")) return false;

    // —— 8. WeakSet.prototype ——
    if (!defineMethod(ctx, weakSetProto, "add", weakSetAdd, 1)) return false;
    if (!defineMethod(ctx, weakSetProto, "has", weakSetHas, 1)) return false;
    if (!defineMethod(ctx, weakSetProto, "delete", weakSetDelete, 1)) return false;
    if (!defineTag(ctx, weakSetProto, "WeakSet")) return false;

    // —— 9. 构造函数 ——
    if (!registerCtor(ctx, "Map", mapConstruct, mapProto)) return false;
    if (!registerCtor(ctx, "Set", setConstruct, setProto)) return false;
    if (!registerCtor(ctx, "WeakMap", weakMapConstruct, weakMapProto)) return false;
    if (!registerCtor(ctx, "WeakSet", weakSetConstruct, weakSetProto)) return false;
    return true;
}

}  // namespace neo::core
