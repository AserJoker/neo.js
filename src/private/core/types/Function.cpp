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
#include "core/IGCEngine.h"
#include "core/types/Function.h"
#include "core/types/TypeBehavior.h"

#include <string>
#include <vector>

namespace neo::core {

// —— 运算：走 ToPrimitive（ES2024 §13.15.3 / §13.15.4）——
// 函数对象默认 ToPrimitive 得源码字符串（Function.prototype.toString），
// 故 + 为字符串拼接、算术为 NaN。

Value* FunctionType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::addValues(ctx, a, b);
}
Value* FunctionType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '-', a, b);
}
Value* FunctionType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '*', a, b);
}
Value* FunctionType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '/', a, b);
}
Value* FunctionType::toString(IContext* ctx, Value* self) {
    // ToString(function) = ToPrimitive(STRING) 后递归（ES2024 §7.1.14）
    return detail::toStringValue(ctx, self);
}

namespace {

// 最少参数校验：不足返回 Exception，否则返回 nullptr。
Value* checkMinArgc(IContext* ctx, const FunctionData* d, size_t argc) {
    if (argc >= d->minArgc) return nullptr;
    std::string msg = std::to_string(d->minArgc) + " arguments required, but only " +
                      std::to_string(argc) + " present";
    return detail::typeError(ctx, msg);
}

// 完整 invoke 流程（ADR-022 §3）：作用域切换/恢复 + 返回值归属 + 栈平衡。
// thisArg 由调用方决定（call 用传入的，construct 用新建对象）。
// fn 默认取 d->native；construct 的专用钩子传入 d->constructFn。
//
// 闭包作用域挂载（ADR-036）：closureScope 平时孤立（parent=nullptr），进入最外层
// 调用时 reparent(rootScope) 临时挂上，使 execScope → closureScope → rootScope →
// global 的查找链成立；退出时摘回孤立。重入调用只增计数，不重复挂载/摘除。
Value* invokeNative(IContext* ctx, FunctionData* d, Value* thisArg,
                    size_t argc, Value* argv[], NativeFn fn = nullptr) {
    // 1. 保存调用方的 root/current scope
    Scope* savedRoot = ctx->root();
    Scope* savedCurrent = ctx->current();

    // 2. 切换到被调函数的模块根作用域
    ctx->setRootScope(d->rootScope);

    // 3. 最外层调用才挂载闭包作用域（重入时已挂好，保持外层查找链）
    if (d->activeCalls == 0) {
        d->closureScope->reparent(d->rootScope);
    }
    ++d->activeCalls;

    // 4. 新建临时执行作用域（parent=closureScope，设为 current）：
    //    查找链 execScope → closureScope → rootScope → global 自然经过闭包作用域。
    //    native 执行期间的临时值归属 execScope，调用结束销毁回收，
    //    不污染持久的 closureScope（后者属函数对象，生命周期随 FunctionData）。
    Scope* execScope = ctx->pushNewScope(d->closureScope);
    if (execScope == nullptr) {
        // 临时 scope 分配失败（OOM）：撤销挂载并恢复 root，返回 OOM 哨兵
        if (--d->activeCalls == 0) {
            d->closureScope->reparent(nullptr);
        }
        ctx->setRootScope(savedRoot);
        return ctx->oomValue();
    }

    // 5. native 调用（构造钩子优先由调用方显式传入）
    Value* res = (fn != nullptr ? fn : d->native)(ctx, thisArg, argc, argv);

    // 6. Fatal 立即透传：不恢复现场、不平衡栈（不可捕获，交给最外层宿主）。
    //    仍须摘除闭包作用域——否则它挂在 root 下，会被 ~Context 的 root 递归析构，
    //    而 FunctionData 析构时再 destroySelf 就是悬垂访问。遗留的 execScope 是
    //    closureScope 的子，随闭包作用域一起销毁。
    if (res != nullptr && res->type()->kind() == TypeKind::Fatal) {
        if (--d->activeCalls == 0) {
            d->closureScope->reparent(nullptr);
        }
        return res;
    }

    // 7. 返回值立即归属调用方 current（restoreValue，ADR-022 §4）：
    //    必须在 pop 之前——res 可能被 execScope 跟踪，先 add 到 savedCurrent（归属调用方）
    //    才能保证 pop 销毁 execScope 后 res 仍存活（否则语义上悬垂，仅靠 GC 侥幸不崩）。
    if (res != nullptr) {
        savedCurrent->add(res);
    }

    // 8. 平衡栈：native 协议允许栈不平衡（如保存现场后不立即返回），
    //    循环 pop 直到 current 回到 closureScope（查找链的持久层）。
    while (ctx->current() != d->closureScope) {
        ctx->popScope();
    }

    // 9. 最外层调用退出时摘除闭包作用域，恢复孤立状态（不再是 GC 根）
    if (--d->activeCalls == 0) {
        d->closureScope->reparent(nullptr);
    }

    // 10. 恢复调用方 root/current（closureScope 不在调用方链上，须显式恢复）
    ctx->setCurrentScope(savedCurrent);
    ctx->setRootScope(savedRoot);
    return res;
}

}  // namespace

Value* FunctionType::call(IContext* ctx, Value* self, Value* thisArg,
                          size_t argc, Value* argv[]) {
    auto* d = static_cast<FunctionData*>(self->data());

    // 绑定函数（bind 产物）：以 boundThis 为 this，前置 boundArgs 再拼接实参。
    // ES2024 §10.4.1.3 [[Call]]。
    if (d->boundTarget != nullptr) {
        std::vector<Value*> args;
        args.reserve(d->boundArgs.size() + argc);
        args.insert(args.end(), d->boundArgs.begin(), d->boundArgs.end());
        if (argc > 0) args.insert(args.end(), argv, argv + argc);
        return ctx->call(d->boundTarget, d->boundThis,
                         static_cast<size_t>(args.size()), args.data());
    }

    if (Value* err = checkMinArgc(ctx, d, argc)) return err;
    return invokeNative(ctx, d, thisArg, argc, argv);
}

// 析构：销毁孤立闭包作用域（ADR-036）。其 values 随作用域一并清空，
// 这些 Value 若已不可达，由下一轮 GC 回收（本轮标记阶段它们仍是根，不会误回收）。
// 若 Fatal 路径遗留了挂载状态，~Scope 会先把自己从 rootScope 摘除。
FunctionData::~FunctionData() {
    if (closureScope != nullptr) {
        closureScope->destroySelf();
        closureScope = nullptr;
    }
}

void FunctionData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
    if (boundTarget != nullptr) visitor->visit(boundTarget);
    if (boundThis != nullptr) visitor->visit(boundThis);
    for (Value* v : boundArgs) {
        if (v != nullptr) visitor->visit(v);
    }
    // 孤立闭包作用域不在 global 作用域树上，只能由持有它的函数对象主动标记
    // （ADR-044）。函数对象不可达时本方法不会被调用，闭包作用域及其捕获值
    // 自然不被标记——「函数 → closureScope → 捕获值 → 函数」的环因此可回收。
    if (closureScope != nullptr) visitor->visitScope(closureScope);
}

// 构造调用（new，ADR-034）：
//   1. 新建 this 对象，原型 = fn.prototype（非 Object/null 时按 null 处理）
//   2. 以 this 为 thisArg 调 native（new 强制新对象，忽略 bind 的 self）
//   3. native 返回 Object/Function 则用它，否则用 this；Exception/Fatal 直接透传
Value* FunctionType::construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    auto* d = static_cast<FunctionData*>(self->data());
    if (Value* err = checkMinArgc(ctx, d, argc)) return err;

    // 构造钩子：call/construct 语义不同的内建（包装类）走专用路径。
    // 钩子负责建对象、挂原型、设置内部槽，并返回对象（或 Exception/Fatal）。
    if (d->constructFn != nullptr) {
        return invokeNative(ctx, d, nullptr, argc, argv, d->constructFn);
    }

    // 取 fn.prototype（沿原型链查找，与 JS 一致）
    Value* proto = ctx->getProperty(self, "prototype");
    TypeKind pk = proto->type()->kind();
    if (!detail::isObjectKind(pk)) {
        // 非对象原型不显式挂链：新对象保留 createObject 的默认原型（Object.prototype）
        proto = nullptr;
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
    // 构造函数显式返回对象（含迭代器）则用它，否则用新建的 this
    if (detail::isObjectKind(rk)) return res;
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
