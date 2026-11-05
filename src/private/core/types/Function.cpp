// Function.cpp - FunctionType 行为方法实现
//
// 本阶段仅实现 call（native 调用）；其余行为返回 Exception("not implement")，
// 待未来包装类（对象/属性）阶段补齐。
//
// call 流程：
//   1. argc < minArgc → Exception("<minArgc> arguments required, but only <argc> present")
//   2. 调 native(ctx, thisArg, argc, argv)，返回值直接透传（含 Exception/Fatal）
//
// 关联 ADR: 005（native 等价）、021（Function 结构）、022（调用）

#include "core/IContext.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Function.h"

#include <string>

namespace neo::core {

namespace {

// 非 call 行为的统一占位错误（待包装类阶段补齐）。
Value* notImplemented(IContext* ctx, const char* op) {
    std::string msg = std::string("Function.") + op + " is not implemented";
    return ctx->createException(ctx->createString(msg));
}

}  // namespace

Value* FunctionType::add(IContext* ctx, Value* /*a*/, Value* /*b*/) {
    return notImplemented(ctx, "add");
}
Value* FunctionType::sub(IContext* ctx, Value* /*a*/, Value* /*b*/) {
    return notImplemented(ctx, "sub");
}
Value* FunctionType::mul(IContext* ctx, Value* /*a*/, Value* /*b*/) {
    return notImplemented(ctx, "mul");
}
Value* FunctionType::div(IContext* ctx, Value* /*a*/, Value* /*b*/) {
    return notImplemented(ctx, "div");
}
Value* FunctionType::toString(IContext* ctx, Value* /*self*/) {
    return notImplemented(ctx, "toString");
}

namespace {

// 最少参数校验：不足返回 Exception，否则返回 nullptr。
Value* checkMinArgc(IContext* ctx, const FunctionData* d, size_t argc) {
    if (argc >= d->minArgc) return nullptr;
    std::string msg = std::to_string(d->minArgc) + " arguments required, but only " +
                      std::to_string(argc) + " present";
    return ctx->createException(ctx->createString(msg));
}

// 完整 invoke 流程（ADR-022 §3）：作用域切换/恢复 + 返回值归属 + 栈平衡。
// thisArg 由调用方决定（call 用传入的，construct 用新建对象）。
Value* invokeNative(IContext* ctx, const FunctionData* d, Value* thisArg,
                    size_t argc, Value* argv[]) {
    // 1. 保存调用方的 root/current scope
    Scope* savedRoot = ctx->root();
    Scope* savedCurrent = ctx->current();

    // 2. 切换到被调函数的模块根作用域
    ctx->setRootScope(d->rootScope);

    // 3. 新建临时执行作用域（parent=closureScope，设为 current）：
    //    查找链 execScope → closureScope → rootScope → global 自然经过闭包作用域。
    //    native 执行期间的临时值归属 execScope，调用结束销毁回收，
    //    不污染持久的 closureScope（后者属函数对象，生命周期由作用域树管理）。
    Scope* execScope = ctx->pushNewScope(d->closureScope);
    if (execScope == nullptr) {
        // 临时 scope 分配失败（OOM）：恢复 root 并返回 OOM 哨兵
        ctx->setRootScope(savedRoot);
        return ctx->oomValue();
    }

    // 4. native 调用
    Value* res = d->native(ctx, thisArg, argc, argv);

    // 5. Fatal 立即透传：不恢复现场、不平衡栈（不可捕获，交给最外层宿主）
    if (res != nullptr && res->type()->kind() == TypeKind::Fatal) {
        return res;
    }

    // 6. 返回值立即归属调用方 current（restoreValue，ADR-022 §4）：
    //    必须在 pop 之前——res 可能被 execScope 跟踪，先 add 到 savedCurrent（scopeRef++）
    //    才能保证 pop 销毁 execScope 后 res 仍存活（否则语义上悬垂，仅靠 GC 侥幸不崩）。
    if (res != nullptr) {
        savedCurrent->add(res);
    }

    // 7. 平衡栈：native 协议允许栈不平衡（如保存现场后不立即返回），
    //    循环 pop 直到 current 回到 closureScope（查找链的持久层）。
    while (ctx->current() != d->closureScope) {
        ctx->popScope();
    }

    // 8. 恢复调用方 root/current（closureScope 不在调用方链上，须显式恢复）
    ctx->setCurrentScope(savedCurrent);
    ctx->setRootScope(savedRoot);
    return res;
}

}  // namespace

Value* FunctionType::call(IContext* ctx, Value* self, Value* thisArg,
                          size_t argc, Value* argv[]) {
    auto* d = static_cast<FunctionData*>(self->data());
    if (Value* err = checkMinArgc(ctx, d, argc)) return err;
    return invokeNative(ctx, d, thisArg, argc, argv);
}

// 构造调用（new，ADR-034）：
//   1. 新建 this 对象，原型 = fn.prototype（非 Object/null 时按 null 处理）
//   2. 以 this 为 thisArg 调 native（new 强制新对象，忽略 bind 的 self）
//   3. native 返回 Object/Function 则用它，否则用 this；Exception/Fatal 直接透传
Value* FunctionType::construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    auto* d = static_cast<FunctionData*>(self->data());
    if (Value* err = checkMinArgc(ctx, d, argc)) return err;

    // 取 fn.prototype（沿原型链查找，与 JS 一致）
    Value* proto = ctx->getProperty(self, "prototype");
    TypeKind pk = proto->type()->kind();
    if (pk != TypeKind::Object && pk != TypeKind::Function && pk != TypeKind::Array) {
        proto = nullptr;  // 非对象原型按 null 处理（无 Object.prototype 兜底）
    }

    // 新建 this 对象并挂原型
    Value* thisObj = ctx->createObject();
    if (thisObj == ctx->oomValue()) return thisObj;
    if (proto != nullptr) {
        Value* r = ctx->setPrototype(thisObj, proto);
        if (r->type()->kind() == TypeKind::Exception) return r;
    }

    Value* res = invokeNative(ctx, d, thisObj, argc, argv);
    if (res == nullptr) return thisObj;
    // 异常/致命错误直接透传（new 失败）
    TypeKind rk = res->type()->kind();
    if (rk == TypeKind::Exception || rk == TypeKind::Fatal) return res;
    // 构造函数显式返回对象（Object/Function/Array）则用它，否则用新建的 this
    if (rk == TypeKind::Object || rk == TypeKind::Function || rk == TypeKind::Array) {
        return res;
    }
    return thisObj;
}

// 属性/原型操作：FunctionData 派生自 ObjectData，直接委托 ObjectType 的实现
// （属性查找沿原型链、OrdinarySet、环检测等逻辑与普通对象完全一致）。
Value* FunctionType::getProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->getProperty(ctx, self, key);
}
Value* FunctionType::setProperty(IContext* ctx, Value* self, Value* key, Value* value) {
    return getObjectType()->setProperty(ctx, self, key, value);
}
bool FunctionType::hasProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->hasProperty(ctx, self, key);
}
Value* FunctionType::deleteProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->deleteProperty(ctx, self, key);
}
Value* FunctionType::defineProperty(IContext* ctx, Value* self, Value* key,
                                    Value* value, bool writable, bool enumerable,
                                    bool configurable) {
    return getObjectType()->defineProperty(ctx, self, key, value, writable, enumerable,
                                           configurable);
}
Value* FunctionType::defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                                    Value* setter, bool enumerable, bool configurable) {
    return getObjectType()->defineAccessor(ctx, self, key, getter, setter, enumerable,
                                           configurable);
}
Value* FunctionType::getPrototype(IContext* ctx, Value* self) {
    return getObjectType()->getPrototype(ctx, self);
}
Value* FunctionType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    return getObjectType()->setPrototype(ctx, self, proto);
}

}  // namespace neo::core
