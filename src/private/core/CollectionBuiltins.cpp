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
#include "core/types/Symbol.h"
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

// 已删除条目保留为「空槽」（key == nullptr），以维持 forEach/迭代器的
// 原始插入顺序下标（§24.1.3.5 NOTE：删除后重加会被再次访问）。
size_t findEntry(const CollectionData* d, Value* key) {
    for (size_t i = 0; i < d->entries.size(); ++i) {
        if (d->entries[i].key != nullptr && sameValueZero(d->entries[i].key, key)) {
            return i;
        }
    }
    return static_cast<size_t>(-1);
}

size_t liveEntryCount(const CollectionData* d) {
    size_t count = 0;
    for (const CollectionEntry& entry : d->entries) {
        if (entry.key != nullptr) ++count;
    }
    return count;
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
    if (detail::isObjectKind(k)) return true;
    // 仅未注册的 Symbol 可被弱引用持有（Symbol.for 的不行）。
    if (k == TypeKind::Symbol) {
        return !static_cast<SymbolData*>(v->data())->registered;
    }
    return false;
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

// AddEntriesFromIterable（§24.1.1.2 / §24.2.1.2）：懒迭代，逐项经 adder 写入。
// mapEntry=true 时每项须为对象并取 "0"/"1"；否则整项作为 Set 值。
Value* addEntriesFromIterable(IContext* ctx, Value* target, Value* iterable,
                              const char* adderName, bool mapEntry) {
    if (iterable->type()->kind() == TypeKind::Undefined ||
        iterable->type()->kind() == TypeKind::Null) {
        return nullptr;
    }
    Value* adder = ctx->getProperty(target, adderName);
    if (detail::isErrorValue(adder)) return adder;
    if (!detail::isCallable(adder)) {
        return detail::typeError(ctx, std::string("'") + adderName +
                                          "' is not a function");
    }
    Value* iterator = detail::getIterator(ctx, iterable);
    if (detail::isErrorValue(iterator)) return iterator;
    for (;;) {
        Value* result = detail::iteratorNext(ctx, iterator);
        if (detail::isErrorValue(result)) {
            return detail::iteratorClose(ctx, iterator, result);
        }
        Value* err = nullptr;
        const bool done = detail::iteratorComplete(ctx, result, err);
        if (err != nullptr) return detail::iteratorClose(ctx, iterator, err);
        if (done) return nullptr;
        Value* item = detail::iteratorValue(ctx, result);
        if (detail::isErrorValue(item)) {
            return detail::iteratorClose(ctx, iterator, item);
        }
        Value* args[2];
        size_t n = 1;
        if (mapEntry) {
            if (!detail::isObjectKind(item->type()->kind())) {
                return detail::iteratorClose(
                    ctx, iterator,
                    detail::typeError(ctx, "Iterator value is not an entry object"));
            }
            Value* key = ctx->getProperty(item, "0");
            if (detail::isErrorValue(key)) {
                return detail::iteratorClose(ctx, iterator, key);
            }
            Value* value = ctx->getProperty(item, "1");
            if (detail::isErrorValue(value)) {
                return detail::iteratorClose(ctx, iterator, value);
            }
            args[0] = key;
            args[1] = value;
            n = 2;
        } else {
            args[0] = item;
        }
        Value* r = ctx->call(adder, target, n, args);
        if (detail::isErrorValue(r)) {
            return detail::iteratorClose(ctx, iterator, r);
        }
    }
}

Value* mapConstruct(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* map = ctx->createCollection(CollectionKind::Map);
    if (detail::isErrorValue(map)) return map;
    if (argc == 0) return map;
    if (Value* err = addEntriesFromIterable(ctx, map, argv[0], "set", true)) {
        return err;
    }
    return map;
}

// Map.groupBy(items, callbackfn)（ES2024 §24.1.2.4）：按回调返回值分组。
Value* mapGroupBy(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* items = argc > 0 ? argv[0] : ctx->createUndefined();
    Value* callback = argc > 1 ? argv[1] : ctx->createUndefined();
    if (!detail::isCallable(callback)) {
        return detail::typeError(ctx, "Map.groupBy callback must be callable");
    }
    Value* map = ctx->createCollection(CollectionKind::Map);
    if (detail::isErrorValue(map)) return map;
    Value* iterator = detail::getIterator(ctx, items);
    if (detail::isErrorValue(iterator)) return iterator;
    double index = 0.0;
    for (;;) {
        Value* result = detail::iteratorNext(ctx, iterator);
        if (detail::isErrorValue(result)) {
            return detail::iteratorClose(ctx, iterator, result);
        }
        Value* err = nullptr;
        const bool done = detail::iteratorComplete(ctx, result, err);
        if (err != nullptr) return detail::iteratorClose(ctx, iterator, err);
        if (done) return map;
        Value* value = detail::iteratorValue(ctx, result);
        if (detail::isErrorValue(value)) {
            return detail::iteratorClose(ctx, iterator, value);
        }
        Value* args[2] = {value, ctx->createNumber(index)};
        Value* key = ctx->call(callback, ctx->createUndefined(), 2, args);
        if (detail::isErrorValue(key)) {
            return detail::iteratorClose(ctx, iterator, key);
        }
        // coercion=zero：-0 归一为 +0（§24.1.2.4 GroupBy step 6.h）。
        if (key->type()->kind() == TypeKind::Number) {
            const double n = static_cast<NumberData*>(key->data())->value;
            if (n == 0.0 && std::signbit(n)) key = ctx->createNumber(0.0);
        }
        CollectionData* d = asCollection(map);
        const size_t found = findEntry(d, key);
        Value* group = nullptr;
        if (found == static_cast<size_t>(-1)) {
            group = ctx->createArray();
            if (detail::isErrorValue(group)) return group;
            setEntry(d, key, group);
        } else {
            group = d->entries[found].value;
        }
        auto* ad = static_cast<ArrayData*>(group->data());
        ad->setElement(ad->length(), value);
        index += 1.0;
    }
}

Value* setConstruct(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* set = ctx->createCollection(CollectionKind::Set);
    if (detail::isErrorValue(set)) return set;
    if (argc == 0) return set;
    if (Value* err = addEntriesFromIterable(ctx, set, argv[0], "add", false)) {
        return err;
    }
    return set;
}

Value* weakMapConstruct(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* map = ctx->createCollection(CollectionKind::WeakMap);
    if (detail::isErrorValue(map)) return map;
    if (argc == 0) return map;
    if (Value* err = addEntriesFromIterable(ctx, map, argv[0], "set", true)) {
        return err;
    }
    return map;
}

Value* weakSetConstruct(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    Value* set = ctx->createCollection(CollectionKind::WeakSet);
    if (detail::isErrorValue(set)) return set;
    if (argc == 0) return set;
    if (Value* err = addEntriesFromIterable(ctx, set, argv[0], "add", false)) {
        return err;
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
    d->entries[index].key = nullptr;
    d->entries[index].value = nullptr;
    return ctx->createBoolean(true);
}

Value* collectionClear(IContext* ctx, Value* self, CollectionKind kind,
                       const char* method) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, kind, method, d)) return err;
    d->entries.clear();
    return ctx->createUndefined();
}

Value* mapClear(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return collectionClear(ctx, self, CollectionKind::Map, "clear");
}

Value* setClear(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return collectionClear(ctx, self, CollectionKind::Set, "clear");
}

Value* mapSizeGet(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::Map, "size", d))
        return err;
    return ctx->createNumber(static_cast<double>(liveEntryCount(d)));
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
        if (d->entries[i].key == nullptr) continue;
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
    d->entries[index].key = nullptr;
    d->entries[index].value = nullptr;
    return ctx->createBoolean(true);
}

Value* setSizeGet(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    CollectionData* d = nullptr;
    if (Value* err = requireCollection(ctx, self, CollectionKind::Set, "size", d))
        return err;
    return ctx->createNumber(static_cast<double>(liveEntryCount(d)));
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
        if (d->entries[i].key == nullptr) continue;
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
    d->entries[index].key = nullptr;
    d->entries[index].value = nullptr;
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
    d->entries[index].key = nullptr;
    d->entries[index].value = nullptr;
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
    if (d == nullptr) {
        it->collection = nullptr;
        return createIterResultObject(ctx, ctx->createUndefined(), true);
    }
    // 跳过已删除的空槽，保持原始插入顺序。
    while (it->index < d->entries.size() &&
           d->entries[it->index].key == nullptr) {
        ++it->index;
    }
    if (it->index >= d->entries.size()) {
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

// 内建方法契约：不可构造、无自有 prototype、length 与最小实参数分离。
FunctionCreateOptions builtinMethodOptions() {
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;
    return opts;
}

bool defineMethod(IContext* ctx, Value* target, const char* name, NativeFn fn,
                  size_t minArgc) {
    Value* f = ctx->createNativeFunction(fn, minArgc, {},
                                         builtinMethodOptions());
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, name);
    if (ctx->defineProperty(f, "length", ctx->createNumber(static_cast<double>(minArgc)), false,
                            false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    return ctx->defineProperty(target, name, f, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

bool defineGetter(IContext* ctx, Value* target, const char* name, NativeFn getter) {
    Value* f = ctx->createNativeFunction(getter, 0, {},
                                         builtinMethodOptions());
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, std::string("get ") + name);
    return ctx->defineAccessor(target, name, f, nullptr, false, true)
               ->type()->kind() != TypeKind::Exception;
}

// @@species getter：返回 this（§24.1.2.2 / §24.2.2.2）。
Value* collectionSpeciesGetter(IContext* /*ctx*/, Value* self, size_t /*argc*/,
                               Value* /*argv*/[]) {
    return self;
}

bool defineSymbolGetter(IContext* ctx, Value* target, WellKnownSymbol which,
                        const char* name, NativeFn getter) {
    Value* f = ctx->createNativeFunction(getter, 0, {},
                                         builtinMethodOptions());
    if (f == ctx->oomValue()) return false;
    ctx->setFunctionName(f, std::string("get ") + name);
    return ctx->defineAccessor(target, ctx->getWellKnownSymbol(which), f,
                               nullptr, false, true)
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
    if (!defineMethod(ctx, mapProto, "clear", mapClear, 0)) return false;
    if (!defineMethod(ctx, mapProto, "forEach", mapForEach, 1)) return false;
    if (!defineMethod(ctx, mapProto, "keys", mapKeys, 0)) return false;
    if (!defineMethod(ctx, mapProto, "values", mapValues, 0)) return false;
    if (!defineMethod(ctx, mapProto, "entries", mapEntries, 0)) return false;
    if (!defineGetter(ctx, mapProto, "size", mapSizeGet)) return false;
    if (!defineTag(ctx, mapProto, "Map")) return false;
    // Map.prototype[Symbol.iterator] 与 entries 是同一个函数对象（§24.1.3.12）。
    {
        Value* entriesFn = ctx->getProperty(mapProto, "entries");
        if (detail::isErrorValue(entriesFn)) return false;
        if (ctx->defineProperty(mapProto,
                                ctx->getWellKnownSymbol(WellKnownSymbol::Iterator),
                                entriesFn, true, false, true)
                ->type()->kind() == TypeKind::Exception) {
            return false;
        }
    }

    // —— 6. Set.prototype ——
    if (!defineMethod(ctx, setProto, "add", setAdd, 1)) return false;
    if (!defineMethod(ctx, setProto, "has", setHas, 1)) return false;
    if (!defineMethod(ctx, setProto, "delete", setDelete, 1)) return false;
    if (!defineMethod(ctx, setProto, "clear", setClear, 0)) return false;
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
    // Set.prototype[Symbol.iterator] 与 values 是同一个函数对象（§24.2.3.11）。
    if (ctx->defineProperty(setProto,
                            ctx->getWellKnownSymbol(WellKnownSymbol::Iterator),
                            setValuesFn, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
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

    // Map / Set 的 @@species getter（§24.1.2.2 / §24.2.2.2）。
    for (const char* name : {"Map", "Set"}) {
        Value* ctor = ctx->getProperty(ctx->globalObject(), name);
        if (detail::isErrorValue(ctor)) return false;
        if (!defineSymbolGetter(ctx, ctor, WellKnownSymbol::Species,
                                "[Symbol.species]", collectionSpeciesGetter)) {
            return false;
        }
    }

    // Map.groupBy（ES2024 §24.1.2.4）。
    {
        Value* mapCtor = ctx->getProperty(ctx->globalObject(), "Map");
        if (detail::isErrorValue(mapCtor)) return false;
        if (!defineMethod(ctx, mapCtor, "groupBy", mapGroupBy, 2)) return false;
    }
    return true;
}

}  // namespace neo::core
