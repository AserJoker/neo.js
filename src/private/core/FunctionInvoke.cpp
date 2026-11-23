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
#include "vm/VM.h"

#include <vector>

namespace neo::core {

namespace {

// 创建并绑定 arguments 到函数执行作用域（§10.4.4）。
// 成功返回 nullptr；失败返回错误值（OOM 等）。
Value* bindArgumentsObject(IContext* ctx, Scope* execScope, Value* fn,
                           size_t argc, Value* argv[], bool mapped) {
    Value* args = ctx->createArgumentsObject(fn, argc, argv, mapped);
    if (detail::isErrorValue(args)) return args;
    execScope->bind("arguments", args);
    return nullptr;
}

// 普通调用的 this 绑定（§10.4.3 OrdinaryCallBindThis）：
// 严格函数原样使用；非严格函数把 null/undefined 换成全局对象，原始值装箱。
// 箭头/绑定函数已由 d->self 固定 this，不在此处替换。
Value* ordinaryCallThis(IContext* ctx, FunctionData* d, Value* thisArg) {
    Value* value = thisArg != nullptr ? thisArg : ctx->createUndefined();
    if (detail::isErrorValue(value)) return value;
    if (d->closureScope != nullptr && d->closureScope->hasFlag("strict")) {
        return value;
    }
    TypeKind kind = value->type()->kind();
    if (kind == TypeKind::Null || kind == TypeKind::Undefined) {
        return ctx->globalObject();
    }
    if (!detail::isObjectKind(kind)) {
        return ctx->createPrimitiveWrapper(value);
    }
    return value;
}

}  // namespace

Value* invokeNativeFunction(IContext* ctx, Value* fn, Value* thisArg, size_t argc,
                            Value* argv[], NativeFn overrideFn, Scope* resumeScope,
                            Value* newTarget, Value** outThis) {
    auto* d = static_cast<FunctionData*>(fn->data());

    // 1. 保存调用方的 root/current scope 与当前函数环境（ADR-047）
    Scope* savedRoot = ctx->root();
    Scope* savedCurrent = ctx->current();
    Scope* savedFunctionEnv = ctx->currentFunctionEnv();
    Value* savedFunction = ctx->currentFunction();

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
            ctx->setCurrentFunction(savedFunction);
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
    ctx->setCurrentFunction(fn);

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
        ctx->setCurrentFunction(savedFunction);
        return res;
    }

    // 7. 返回值立即归属调用方 current（restoreValue，ADR-022 §4）：
    //    必须在 pop 之前——res 可能被 execScope 跟踪，先 add 到 savedCurrent（归属调用方）
    //    才能保证 pop 销毁 execScope 后 res 仍存活（否则语义上悬垂，仅靠 GC 侥幸不崩）。
    if (res != nullptr) {
        savedCurrent->add(res);
    }

    // 8. 协程挂起：Interrupt(Yield/Await) 且当前在协程恢复中 → 保留 execScope
    if (res != nullptr && detail::isInterruptValue(res) &&
        isSuspensionSignal(asInterrupt(res)->kind)) {
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
            ctx->setCurrentFunction(savedFunction);
            return res;
        }
    }

    // 9. 平衡栈：native 协议允许栈不平衡（如保存现场后不立即返回），
    //    循环 pop 直到 current 回到 closureScope（查找链的持久层）。
    //    先把函数环境恢复为调用方，避免 execScope 被 pop 后缓存悬垂。
    ctx->setCurrentFunctionEnv(savedFunctionEnv);
    ctx->setCurrentFunction(savedFunction);
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

Value* invokeScriptFunction(IContext* ctx, Value* fn, Value* thisArg, size_t argc,
                            Value* argv[], Value* newTarget, Value** outThis) {
    // 尾调用实参存储：跨迭代存活（被调函数执行期间 argv 指向它）。
    std::vector<Value*> tailArgsStorage;
    // 上一次尾调用信号：根在调用方作用域，保证实参在被调函数执行期间存活；
    // 被调函数返回后立即移除，避免 10 万次尾调用在作用域里堆积不可回收的值。
    Value* pendingTailRoot = nullptr;
    for (;;) {
        auto* d = static_cast<FunctionData*>(fn->data());
        if (d->code == nullptr) {
            return detail::typeError(ctx, "script function has no code");
        }

        // 1. 保存调用方状态
        Scope* savedRoot = ctx->root();
        Scope* savedCurrent = ctx->current();
        Scope* savedFunctionEnv = ctx->currentFunctionEnv();
        Value* savedFunction = ctx->currentFunction();

        // 2. 切换到被调函数的模块根作用域，最外层调用挂载闭包作用域
        ctx->setRootScope(d->rootScope);
        if (!d->capturesEnvironment && d->activeCalls == 0) {
            d->closureScope->reparent(d->rootScope);
        }
        ++d->activeCalls;

        // 3. 执行作用域（execScope → closureScope → rootScope → global）
        Scope* execScope = ctx->pushNewScope(d->closureScope);
        if (execScope == nullptr) {
            if (--d->activeCalls == 0 && !d->capturesEnvironment) {
                d->closureScope->reparent(nullptr);
            }
            ctx->setRootScope(savedRoot);
            ctx->setCurrentScope(savedCurrent);
            ctx->setCurrentFunctionEnv(savedFunctionEnv);
            ctx->setCurrentFunction(savedFunction);
            return ctx->oomValue();
        }

        // this 优先级：函数自身绑定（箭头/bind）> 调用点传入（按严格性替换）。
        Value* boundThis =
            d->self != nullptr ? d->self : ordinaryCallThis(ctx, d, thisArg);
        if (detail::isErrorValue(boundThis)) {
            while (ctx->current() != d->closureScope) ctx->popScope();
            if (--d->activeCalls == 0 && !d->capturesEnvironment) {
                d->closureScope->reparent(nullptr);
            }
            ctx->setCurrentScope(savedCurrent);
            ctx->setRootScope(savedRoot);
            ctx->setCurrentFunctionEnv(savedFunctionEnv);
            ctx->setCurrentFunction(savedFunction);
            return boundThis;
        }
        execScope->setThis(boundThis);
        execScope->setNewTarget(newTarget != nullptr ? newTarget
                                                     : ctx->createUndefined());
        ctx->setCurrentFunctionEnv(execScope);
        ctx->setCurrentFunction(fn);

        // 4. arguments 对象（§10.4.4）：在形参绑定与函数体执行前创建，
        //    使默认参数表达式与 eval 也能看到它。
        if (d->needsArguments) {
            Value* err = bindArgumentsObject(ctx, execScope, fn, argc, argv,
                                             d->argumentsMapped);
            if (err != nullptr) {
                ctx->setCurrentFunctionEnv(savedFunctionEnv);
                ctx->setCurrentFunction(savedFunction);
                while (ctx->current() != d->closureScope) ctx->popScope();
                if (--d->activeCalls == 0 && !d->capturesEnvironment) {
                    d->closureScope->reparent(nullptr);
                }
                ctx->setCurrentScope(savedCurrent);
                ctx->setRootScope(savedRoot);
                return err;
            }
        }

        // 5. 新 VM：逆序压形参（缺省补 undefined），self 最后压入（栈顶）。
        //    rest 函数需保留全部实参，由序言 RestArgs 收集多余部分。
        vm::VM machine(ctx, d->code);
        size_t total = d->paramCount;
        if (d->hasRest && argc > total) total = argc;
        for (size_t i = total; i > 0; --i) {
            size_t index = i - 1;
            machine.push(index < argc ? argv[index] : ctx->createUndefined());
        }
        machine.push(boundThis);

        Value* res = machine.run(d->entryPc);

        // 5. Fatal：不恢复现场、不平衡栈（不可捕获），只摘除闭包挂载
        if (res != nullptr && res->type()->kind() == TypeKind::Fatal) {
            if (--d->activeCalls == 0 && !d->capturesEnvironment) {
                d->closureScope->reparent(nullptr);
            }
            ctx->setRootScope(savedRoot);
            ctx->setCurrentScope(savedCurrent);
            ctx->setCurrentFunctionEnv(savedFunctionEnv);
            ctx->setCurrentFunction(savedFunction);
            return res;
        }

        // 6. 返回值归属调用方作用域（restoreValue 语义）
        if (res != nullptr) savedCurrent->add(res);
        if (outThis != nullptr) *outThis = execScope->thisValue();

        // 7. 平衡作用域并恢复调用方
        ctx->setCurrentFunctionEnv(savedFunctionEnv);
        ctx->setCurrentFunction(savedFunction);
        while (ctx->current() != d->closureScope) {
            ctx->popScope();
        }
        if (--d->activeCalls == 0 && !d->capturesEnvironment) {
            d->closureScope->reparent(nullptr);
        }
        ctx->setCurrentScope(savedCurrent);
        ctx->setRootScope(savedRoot);

        // 被调函数已返回：解除上一次尾调用信号的根。
        if (pendingTailRoot != nullptr) {
            savedCurrent->remove(pendingTailRoot);
            pendingTailRoot = nullptr;
        }

        // 8. 尾调用：复用当前 C++ 帧执行被调函数（§15.10.1）。
        if (res != nullptr && detail::isInterruptValue(res)) {
            auto* ir = asInterrupt(res);
            if (ir != nullptr && ir->kind == InterruptKind::TailCall) {
                Value* tailFn = ir->tailFn;
                Value* tailThis = ir->tailThis;
                tailArgsStorage = ir->tailArgs;
                if (tailFn != nullptr &&
                    tailFn->type()->kind() == TypeKind::Function &&
                    static_cast<FunctionData*>(tailFn->data())->code != nullptr) {
                    fn = tailFn;
                    thisArg = tailThis;
                    argc = tailArgsStorage.size();
                    argv = tailArgsStorage.data();
                    newTarget = nullptr;
                    // 信号已加入调用方作用域（步骤 6），下一轮返回后移除。
                    pendingTailRoot = res;
                    continue;
                }
                // 非脚本函数：正常调用（当前帧已拆除）。
                return ctx->call(tailFn, tailThis, tailArgsStorage.size(),
                                 tailArgsStorage.data());
            }
        }
        return res;
    }
}

GeneratorPrelude prepareGeneratorCoroutine(IContext* ctx, Value* fn,
                                           Value* thisArg, size_t argc,
                                           Value* argv[]) {
    GeneratorPrelude out;
    auto* d = static_cast<FunctionData*>(fn->data());
    if (d->code == nullptr) {
        out.error = detail::typeError(ctx, "script function has no code");
        return out;
    }

    Scope* savedRoot = ctx->root();
    Scope* savedCurrent = ctx->current();
    Scope* savedFunctionEnv = ctx->currentFunctionEnv();
    Value* savedFunction = ctx->currentFunction();

    ctx->setRootScope(d->rootScope);
    if (!d->capturesEnvironment && d->activeCalls == 0) {
        d->closureScope->reparent(d->rootScope);
    }
    ++d->activeCalls;

    Scope* execScope = ctx->pushNewScope(d->closureScope);
    if (execScope == nullptr) {
        if (--d->activeCalls == 0 && !d->capturesEnvironment) {
            d->closureScope->reparent(nullptr);
        }
        ctx->setRootScope(savedRoot);
        ctx->setCurrentScope(savedCurrent);
        ctx->setCurrentFunctionEnv(savedFunctionEnv);
        ctx->setCurrentFunction(savedFunction);
        out.error = ctx->oomValue();
        return out;
    }

    Value* boundThis =
        d->self != nullptr ? d->self : ordinaryCallThis(ctx, d, thisArg);
    execScope->setThis(boundThis);
    execScope->setNewTarget(ctx->createUndefined());
    ctx->setCurrentFunctionEnv(execScope);
    ctx->setCurrentFunction(fn);

    if (d->needsArguments) {
        Value* err = bindArgumentsObject(ctx, execScope, fn, argc, argv,
                                         d->argumentsMapped);
        if (err != nullptr) {
            ctx->setCurrentFunctionEnv(savedFunctionEnv);
            ctx->setCurrentFunction(savedFunction);
            while (ctx->current() != d->closureScope) ctx->popScope();
            if (--d->activeCalls == 0 && !d->capturesEnvironment) {
                d->closureScope->reparent(nullptr);
            }
            ctx->setCurrentScope(savedCurrent);
            ctx->setRootScope(savedRoot);
            out.error = err;
            return out;
        }
    }

    vm::VM machine(ctx, d->code);
    size_t total = d->paramCount;
    if (d->hasRest && argc > total) total = argc;
    for (size_t i = total; i > 0; --i) {
        size_t index = i - 1;
        machine.push(index < argc ? argv[index] : ctx->createUndefined());
    }
    machine.push(boundThis);

    Value* res = machine.run(d->entryPc);
    const bool bodyStart =
        res != nullptr && detail::isInterruptValue(res) &&
        asInterrupt(res)->kind == InterruptKind::BodyStart;

    if (bodyStart) {
        out.execScope = execScope;
        out.vmState = new vm::VmState(machine.snapshot());
        execScope->reparent(nullptr);
        if (--d->activeCalls == 0 && !d->capturesEnvironment) {
            d->closureScope->reparent(nullptr);
        }
        ctx->setCurrentFunctionEnv(savedFunctionEnv);
        ctx->setCurrentFunction(savedFunction);
        ctx->setCurrentScope(savedCurrent);
        ctx->setRootScope(savedRoot);
        return out;
    }

    // 参数绑定失败（或异常）：调用方直接返回该结果，不创建协程。
    if (res != nullptr && res->type()->kind() == TypeKind::Fatal) {
        if (--d->activeCalls == 0 && !d->capturesEnvironment) {
            d->closureScope->reparent(nullptr);
        }
        ctx->setRootScope(savedRoot);
        ctx->setCurrentScope(savedCurrent);
        ctx->setCurrentFunctionEnv(savedFunctionEnv);
        ctx->setCurrentFunction(savedFunction);
        out.error = res;
        return out;
    }
    if (res != nullptr) savedCurrent->add(res);
    ctx->setCurrentFunctionEnv(savedFunctionEnv);
    ctx->setCurrentFunction(savedFunction);
    while (ctx->current() != d->closureScope) ctx->popScope();
    if (--d->activeCalls == 0 && !d->capturesEnvironment) {
        d->closureScope->reparent(nullptr);
    }
    ctx->setCurrentScope(savedCurrent);
    ctx->setRootScope(savedRoot);
    out.error = res != nullptr ? res : ctx->createUndefined();
    return out;
}

Value* resumeScriptCoroutine(IContext* ctx, Value* coroutine, Value* sentValue) {
    auto* co = asCoroutine(coroutine);
    if (co == nullptr || co->target == nullptr) {
        return detail::typeError(ctx, "coroutine has no target");
    }
    auto* d = static_cast<FunctionData*>(co->target->data());
    if (d->code == nullptr) {
        return detail::typeError(ctx, "script function has no code");
    }

    // 1. 保存调用方状态
    Scope* savedRoot = ctx->root();
    Scope* savedCurrent = ctx->current();
    Scope* savedFunctionEnv = ctx->currentFunctionEnv();
    Value* savedFunction = ctx->currentFunction();

    // 2. 切换到生成器函数的模块根作用域并挂载闭包作用域
    ctx->setRootScope(d->rootScope);
    if (!d->capturesEnvironment && d->activeCalls == 0) {
        d->closureScope->reparent(d->rootScope);
    }
    ++d->activeCalls;

    const bool first = co->execScope == nullptr;
    Scope* execScope = co->execScope;
    if (first) {
        execScope = ctx->pushNewScope(d->closureScope);
        if (execScope == nullptr) {
            if (--d->activeCalls == 0 && !d->capturesEnvironment) {
                d->closureScope->reparent(nullptr);
            }
            ctx->setRootScope(savedRoot);
            ctx->setCurrentScope(savedCurrent);
            ctx->setCurrentFunctionEnv(savedFunctionEnv);
            ctx->setCurrentFunction(savedFunction);
            return ctx->oomValue();
        }
        co->execScope = execScope;
        Value* boundThis =
            d->self != nullptr ? d->self
                               : ordinaryCallThis(ctx, d, co->receiver);
        execScope->setThis(boundThis);
        execScope->setNewTarget(ctx->createUndefined());
    } else {
        // 恢复：重新挂上孤立的执行子树，并从保存的最内层作用域继续。
        execScope->reparent(d->closureScope);
    }
    ctx->setCurrentFunctionEnv(execScope);
    ctx->setCurrentFunction(co->target);

    // 3. 首次运行创建 arguments（生成器/async 在首次 resume 时执行函数体）。
    if (first && d->needsArguments) {
        const size_t argc = co->initialArgs.size();
        Value* err = bindArgumentsObject(ctx, execScope, co->target, argc,
                                         co->initialArgs.data(),
                                         d->argumentsMapped);
        if (err != nullptr) {
            ctx->setCurrentFunctionEnv(savedFunctionEnv);
            ctx->setCurrentFunction(savedFunction);
            while (ctx->current() != d->closureScope) ctx->popScope();
            if (--d->activeCalls == 0 && !d->capturesEnvironment) {
                d->closureScope->reparent(nullptr);
            }
            ctx->setCurrentScope(savedCurrent);
            ctx->setRootScope(savedRoot);
            return err;
        }
    }

    // 4. 首次运行压入形参与 self；后续从 VmState 恢复。
    vm::VM machine(ctx, d->code);
    Value* res = nullptr;
    if (first) {
        size_t argc = co->initialArgs.size();
        size_t total = d->paramCount;
        if (d->hasRest && argc > total) total = argc;
        for (size_t i = total; i > 0; --i) {
            size_t index = i - 1;
            machine.push(index < argc ? co->initialArgs[index]
                                      : ctx->createUndefined());
        }
        machine.push(execScope->thisValue() != nullptr
                         ? execScope->thisValue()
                         : ctx->createUndefined());
        res = machine.run(d->entryPc);
    } else {
        // async script：await 拒绝以 Exception 注入，需转为挂起点的 throw。
        Value* injected = sentValue;
        if (d->asyncScript && sentValue != nullptr &&
            sentValue->type()->kind() == TypeKind::Exception) {
            injected = ctx->createInterrupt(InterruptKind::Throw, 0, sentValue);
        }
        res = machine.resume(*co->vmState, injected);
    }

    const bool suspended =
        res != nullptr && detail::isInterruptValue(res) &&
        isSuspensionSignal(asInterrupt(res)->kind);

    if (suspended) {
        // 保存 VM 状态；execScope 摘成孤立子树，由协程对象持有并标记。
        if (co->vmState == nullptr) co->vmState = new vm::VmState();
        *co->vmState = machine.snapshot();
        co->yielded = asInterrupt(res)->value;
        execScope->reparent(nullptr);
        co->state = CoroutineState::Suspended;
        if (--d->activeCalls == 0 && !d->capturesEnvironment) {
            d->closureScope->reparent(nullptr);
        }
        ctx->setCurrentFunctionEnv(savedFunctionEnv);
        ctx->setCurrentFunction(savedFunction);
        ctx->setCurrentScope(savedCurrent);
        ctx->setRootScope(savedRoot);
        return res;
    }

    // 4. 完成：销毁执行子树、释放 VM 状态。
    if (res != nullptr) savedCurrent->add(res);
    co->state = CoroutineState::Completed;
    co->yielded = res;
    delete co->vmState;
    co->vmState = nullptr;
    ctx->setCurrentFunctionEnv(savedFunctionEnv);
    ctx->setCurrentFunction(savedFunction);
    while (ctx->current() != d->closureScope) {
        ctx->popScope();
    }
    co->execScope = nullptr;
    if (--d->activeCalls == 0 && !d->capturesEnvironment) {
        d->closureScope->reparent(nullptr);
    }
    ctx->setCurrentScope(savedCurrent);
    ctx->setRootScope(savedRoot);
    return res;
}

}  // namespace neo::core
