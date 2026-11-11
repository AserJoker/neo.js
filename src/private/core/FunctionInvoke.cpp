// FunctionInvoke.cpp - native 函数调用核心（ADR-022 §3 + ADR-045 §5）
//
// 完整调用流程：作用域切换/恢复 + 返回值归属 + 栈平衡。
// thisArg 由调用方决定（call 用传入的，construct 用新建对象）。
//
// 闭包作用域挂载（ADR-036）：closureScope 平时孤立（parent=nullptr），进入最外层
// 调用时 reparent(rootScope) 临时挂上，使 execScope → closureScope → rootScope →
// global 的查找链成立；退出时摘回孤立。重入调用只增计数，不重复挂载/摘除。
//
// 协程挂起分支（ADR-045 §5）：处于协程恢复中（ctx->getCurrentCoroutine() 非空）
// 且函数体返回 Interrupt(Yield) 时，**不销毁** execScope，而是交给协程对象、
// 从 closureScope 摘下变成孤立子树；恢复时由 runner 传入 resumeScope 重新挂上。
//
// 关联 ADR: 005（native 等价）、022（调用）、036（闭包作用域孤立化）、
//          045（协程抽象）、046（Interrupt 控制信号）

#include "core/FunctionInvoke.h"

#include "core/IContext.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Coroutine.h"
#include "core/types/Function.h"
#include "core/types/Interrupt.h"
#include "core/types/TypeBehavior.h"

namespace neo::core {

Value* invokeNativeFunction(IContext* ctx, Value* fn, Value* thisArg, size_t argc,
                            Value* argv[], NativeFn overrideFn, Scope* resumeScope,
                            Value* newTarget, Value** outThis) {
    auto* d = static_cast<FunctionData*>(fn->data());

    // 1. 保存调用方的 root/current scope 与当前函数环境（ADR-047）
    Scope* savedRoot = ctx->root();
    Scope* savedCurrent = ctx->current();
    Scope* savedFunctionEnv = ctx->currentFunctionEnv();

    // 2. 切换到被调函数的模块根作用域
    ctx->setRootScope(d->rootScope);

    // 3. 最外层调用才挂载闭包作用域（重入时已挂好，保持外层查找链）。
    //    Environment 模式闭包已常驻定义点环境链，调用期不挂载/摘除。
    if (!d->capturesEnvironment && d->activeCalls == 0) {
        d->closureScope->reparent(d->rootScope);
    }
    ++d->activeCalls;

    // 4. 执行作用域：
    //    恢复协程时重挂保存的孤立子树（resumeScope）；否则新建临时执行作用域。
    //    查找链 execScope → closureScope → rootScope → global 自然经过闭包作用域。
    Scope* execScope = nullptr;
    if (resumeScope != nullptr) {
        resumeScope->reparent(d->closureScope);
        ctx->setCurrentScope(resumeScope);
        execScope = resumeScope;
    } else {
        execScope = ctx->pushNewScope(d->closureScope);
        if (execScope == nullptr) {
            // 临时 scope 分配失败（OOM）：撤销挂载并恢复 root，返回 OOM 哨兵
            if (--d->activeCalls == 0 && !d->capturesEnvironment) {
                d->closureScope->reparent(nullptr);
            }
            ctx->setRootScope(savedRoot);
            ctx->setCurrentScope(savedCurrent);
            ctx->setCurrentFunctionEnv(savedFunctionEnv);
            return ctx->oomValue();
        }
    }

    // 4.5 绑定函数环境（ADR-047）：this / new.target 放 execScope 专用槽。
    //     协程恢复时槽已存在，保留首次绑定的值，不重复覆盖。
    //     普通调用 thisArg 可能为 nullptr（如构造钩子），按 undefined 处理；
    //     newTarget 为 nullptr 表示普通调用。
    if (!execScope->hasOwnThis()) {
        execScope->setThis(thisArg != nullptr ? thisArg : ctx->createUndefined());
    }
    if (!execScope->hasOwnNewTarget()) {
        execScope->setNewTarget(newTarget != nullptr ? newTarget : ctx->createUndefined());
    }
    ctx->setCurrentFunctionEnv(execScope);

    // 5. native 调用（构造钩子优先由调用方显式传入）
    Value* res = (overrideFn != nullptr ? overrideFn : d->native)(ctx, thisArg, argc, argv);

    // 5.5 派生类构造器：this 可能由 super() 初始化，在弹出 execScope 前写出最终值。
    if (outThis != nullptr) *outThis = execScope->thisValue();

    // 6. Fatal 立即透传：不恢复现场、不平衡栈（不可捕获，交给最外层宿主）。
    //    仍须摘除闭包作用域——否则它挂在 root 下，会被 ~Context 的 root 递归析构，
    //    而 FunctionData 析构时再 destroySelf 就是悬垂访问。遗留的 execScope 是
    //    closureScope 的子，随闭包作用域一起销毁。
    if (res != nullptr && res->type()->kind() == TypeKind::Fatal) {
        if (--d->activeCalls == 0) {
            d->closureScope->reparent(nullptr);
        }
        ctx->setRootScope(savedRoot);
        ctx->setCurrentScope(savedCurrent);
        ctx->setCurrentFunctionEnv(savedFunctionEnv);
        return res;
    }

    // 7. 返回值立即归属调用方 current（restoreValue，ADR-022 §4）：
    //    必须在 pop 之前——res 可能被 execScope 跟踪，先 add 到 savedCurrent（归属调用方）
    //    才能保证 pop 销毁 execScope 后 res 仍存活（否则语义上悬垂，仅靠 GC 侥幸不崩）。
    if (res != nullptr) {
        savedCurrent->add(res);
    }

    // 8. 协程挂起：Interrupt(Yield) 且当前在协程恢复中 → 保留 execScope
    if (res != nullptr && detail::isInterruptValue(res) &&
        asInterrupt(res)->kind == InterruptKind::Yield) {
        if (CoroutineData* co = asCoroutine(ctx->getCurrentCoroutine())) {
            InterruptData* ir = asInterrupt(res);
            co->resumePoint = ir->offset;
            co->yielded = ir->value;
            co->execScope = execScope;
            co->state = CoroutineState::Suspended;
            co->savedCallStack = ctx->callStack();
            // 回退 current 到 closureScope，再把 execScope 摘下孤立化
            // （不能 popScope——那会销毁 execScope，跨 yield 状态全部丢失）。
            ctx->setCurrentScope(d->closureScope);
            execScope->reparent(nullptr);
            if (--d->activeCalls == 0 && !d->capturesEnvironment) {
                d->closureScope->reparent(nullptr);
            }
            ctx->setRootScope(savedRoot);
            ctx->setCurrentScope(savedCurrent);
            ctx->setCurrentFunctionEnv(savedFunctionEnv);
            return res;
        }
    }

    // 9. 平衡栈：native 协议允许栈不平衡（如保存现场后不立即返回），
    //    循环 pop 直到 current 回到 closureScope（查找链的持久层）。
    //    先把函数环境恢复为调用方，避免 execScope 被 pop 后缓存悬垂。
    ctx->setCurrentFunctionEnv(savedFunctionEnv);
    while (ctx->current() != d->closureScope) {
        ctx->popScope();
    }

    // 10. 最外层调用退出时摘除闭包作用域，恢复孤立状态（不再是 GC 根）。
    //     Environment 模式常驻定义点环境链，不摘除。
    if (--d->activeCalls == 0 && !d->capturesEnvironment) {
        d->closureScope->reparent(nullptr);
    }

    // 11. 恢复调用方 root/current（closureScope 不在调用方链上，须显式恢复）
    ctx->setCurrentScope(savedCurrent);
    ctx->setRootScope(savedRoot);
    return res;
}

}  // namespace neo::core
