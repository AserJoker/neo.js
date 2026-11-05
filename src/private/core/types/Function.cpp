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

Value* FunctionType::call(IContext* ctx, Value* self, Value* thisArg,
                          size_t argc, Value* argv[]) {
    auto* d = static_cast<FunctionData*>(self->data());
    // 最少参数校验
    if (argc < d->minArgc) {
        std::string msg = std::to_string(d->minArgc) + " arguments required, but only " +
                          std::to_string(argc) + " present";
        return ctx->createException(ctx->createString(msg));
    }

    // —— 完整 invoke 流程（ADR-022 §3）——
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

    // 4. native 调用（this 绑定：本阶段 FunctionData 无 self 字段，直接用传入 thisArg）
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

// 属性/原型操作：FunctionData 派生自 ObjectData，直接委托 ObjectType 的实现
// （属性查找沿原型链、OrdinarySet、环检测等逻辑与普通对象完全一致）。
Value* FunctionType::getProperty(IContext* ctx, Value* self, std::string_view key) {
    return getObjectType()->getProperty(ctx, self, key);
}
Value* FunctionType::setProperty(IContext* ctx, Value* self, std::string_view key,
                                 Value* value) {
    return getObjectType()->setProperty(ctx, self, key, value);
}
bool FunctionType::hasProperty(IContext* ctx, Value* self, std::string_view key) {
    return getObjectType()->hasProperty(ctx, self, key);
}
Value* FunctionType::deleteProperty(IContext* ctx, Value* self, std::string_view key) {
    return getObjectType()->deleteProperty(ctx, self, key);
}
Value* FunctionType::defineProperty(IContext* ctx, Value* self, std::string_view key,
                                    Value* value, bool writable, bool enumerable,
                                    bool configurable) {
    return getObjectType()->defineProperty(ctx, self, key, value, writable, enumerable,
                                           configurable);
}
Value* FunctionType::getPrototype(IContext* ctx, Value* self) {
    return getObjectType()->getPrototype(ctx, self);
}
Value* FunctionType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    return getObjectType()->setPrototype(ctx, self, proto);
}

}  // namespace neo::core
