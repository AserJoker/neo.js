// Module.cpp - ES Module 基础设施实现
//
// 模块 = root scope + 原型为 null 的导出对象，由 Context 以绝对路径为 key 持有。
// 模块 root scope 作为 global 的子作用域挂入作用域树，故其跟踪的 Value
// （含导出对象）从 global 树可达（ADR-044），生命周期由 Context 管理。
//
// import 的本质：惰性创建模块 → 若未加载则在其 root scope 下执行 loader →
// 返回导出对象。循环依赖时返回当前（部分）导出对象。
//
// 绑定模型（ADR-059）：导入是导出方槽位的只读别名；导出是槽位映射。
// native loader 需先提升建槽并 exportSlot，再 importModule，最后 initialize。
// 完整的 Module Instantiation / Linking 由 Compiler/宿主编排。
//
// 每个模块记录自己的 dirname；相对说明符（./ ../）以当前加载模块的 dirname 为基准。
//
// 关联 ADR: 011（Context 门面）、012（Scope 树）、044（可达性标记）、
//          059（模块绑定与槽位共享 import）

#include "core/Context.h"

#include "core/AsyncNative.h"
#include "core/PromiseBuiltins.h"
#include "core/PromiseCapability.h"

#include "core/types/Array.h"
#include "core/types/Coroutine.h"
#include "core/types/Exception.h"
#include "core/types/Interrupt.h"
#include "core/types/ModuleNamespace.h"
#include "core/types/Promise.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "vm/Code.h"
#include "vm/VM.h"

#include <algorithm>
#include <new>
#include <string>
#include <unordered_set>
#include <utility>

namespace neo::core {

// ModuleCompilation 持有 unique_ptr<vm::Code>，而 vm::Code 在公共头中只有
// 前向声明；特殊成员必须在包含完整类型的 TU 里定义。
ModuleCompilation::ModuleCompilation() = default;
ModuleCompilation::~ModuleCompilation() = default;
ModuleCompilation::ModuleCompilation(ModuleCompilation&&) noexcept = default;
ModuleCompilation& ModuleCompilation::operator=(ModuleCompilation&&) noexcept =
    default;

Module::Module() = default;
Module::~Module() = default;

namespace {

bool isAsciiAlpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// 相对模块说明符：以 ./ 或 ../ 开头（JS 中裸说明符不是相对路径）。
bool isRelativeSpecifier(std::string_view p) {
    if (p.empty() || p[0] != '.') return false;
    if (p.size() == 1) return true;
    if (p[1] == '/' || p[1] == '\\') return true;
    return p[1] == '.' && (p.size() == 2 || p[2] == '/' || p[2] == '\\');
}

// 规范化路径：折叠 . / .. 与重复分隔符，统一用 '/'。
std::string normalizePath(std::string_view path) {
    std::string prefix;
    if (path.size() >= 2 && path[1] == ':' && isAsciiAlpha(path[0])) {
        prefix = std::string(path.substr(0, 2)) + "/";
        path.remove_prefix(2);
    } else if (!path.empty() && (path[0] == '/' || path[0] == '\\')) {
        prefix = "/";
    }
    const bool absolute = !prefix.empty();

    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= path.size()) {
        size_t j = i;
        while (j < path.size() && path[j] != '/' && path[j] != '\\') ++j;
        std::string_view seg = path.substr(i, j - i);
        if (seg == "..") {
            if (!parts.empty() && parts.back() != "..") {
                parts.pop_back();
            } else if (!absolute) {
                parts.emplace_back("..");
            }
        } else if (!seg.empty() && seg != ".") {
            parts.emplace_back(seg);
        }
        if (j >= path.size()) break;
        i = j + 1;
    }

    std::string out = prefix;
    for (size_t k = 0; k < parts.size(); ++k) {
        if (k != 0) out.push_back('/');
        out += parts[k];
    }
    return out;
}

// id 的目录部分。无分隔符返回空串（非绝对路径）。
std::string dirnameOf(std::string_view id) {
    size_t pos = id.find_last_of("/\\");
    if (pos == std::string_view::npos) return {};
    if (pos == 0) return "/";
    return std::string(id.substr(0, pos));
}

// 相对说明符按基准目录解析；绝对/裸说明符原样返回。
std::string resolveSpecifier(const std::vector<Module*>& stack,
                             std::string_view specifier) {
    if (!isRelativeSpecifier(specifier) || stack.empty()) {
        return std::string(specifier);
    }
    std::string joined = stack.back()->dirname;
    if (!joined.empty()) joined.push_back('/');
    joined += std::string(specifier);
    return normalizePath(joined);
}

// —— 顶层 await 的 Promise 回调（ADR-064）——

// 取 native 回调闭包作用域里捕获的值（与 AsyncNative 同模式）。
Value* captured(IContext* ctx, const char* name) {
    Scope* cur = ctx->current();
    Scope* closure = cur != nullptr ? cur->parent() : nullptr;
    return closure != nullptr ? closure->lookup(name) : nullptr;
}

Value* undefinedOf(IContext* ctx) { return ctx->createUndefined(); }

// 异常值 → 拒绝原因（Exception 携带的 Error 对象；其他值原样）。
Value* reasonOf(Value* v) {
    if (v != nullptr && v->type()->kind() == TypeKind::Exception) {
        return static_cast<ExceptionData*>(v->data())->child;
    }
    return v;
}

void settle(IContext* ctx, Value* fn, Value* value) {
    if (fn == nullptr) return;
    Value* args[1] = {value};
    (void)ctx->call(fn, undefinedOf(ctx), 1, args);
}

// 模块异步执行完成：AsyncModuleExecutionFulfilled。
Value* moduleAsyncFulfilled(IContext* ctx, Value*, size_t /*argc*/,
                            Value* /*argv*/[]) {
    Value* idValue = captured(ctx, "__modId");
    if (idValue != nullptr && idValue->type()->kind() == TypeKind::String) {
        Module* mod = static_cast<Context*>(ctx)->findModule(
            static_cast<StringData*>(idValue->data())->value);
        if (mod != nullptr) {
            static_cast<Context*>(ctx)->asyncModuleExecutionFulfilled(mod);
        }
    }
    return undefinedOf(ctx);
}

// 模块异步执行拒绝：AsyncModuleExecutionRejected。
Value* moduleAsyncRejected(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* idValue = captured(ctx, "__modId");
    Value* reason = argc > 0 ? argv[0] : undefinedOf(ctx);
    if (idValue != nullptr && idValue->type()->kind() == TypeKind::String) {
        Module* mod = static_cast<Context*>(ctx)->findModule(
            static_cast<StringData*>(idValue->data())->value);
        if (mod != nullptr) {
            Value* exc = reason->type()->kind() == TypeKind::Exception
                             ? reason
                             : ctx->createException(reason);
            static_cast<Context*>(ctx)->asyncModuleExecutionRejected(mod, exc);
        }
    }
    return undefinedOf(ctx);
}

// 动态 import() 等待模块异步求值完成后再 settle 命名空间。
Value* dynamicImportFulfilled(IContext* ctx, Value*, size_t /*argc*/,
                              Value* /*argv*/[]) {
    Value* promise = captured(ctx, "__promise");
    Value* ns = captured(ctx, "__ns");
    if (promise != nullptr && ns != nullptr) promiseFulfill(ctx, promise, ns);
    return undefinedOf(ctx);
}

Value* dynamicImportRejected(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    Value* promise = captured(ctx, "__promise");
    Value* reason = argc > 0 ? argv[0] : undefinedOf(ctx);
    if (promise != nullptr) promiseReject(ctx, promise, reason);
    return undefinedOf(ctx);
}

}  // namespace

bool Context::hasModule(std::string_view id) const {
    return modules_.find(std::string(id)) != modules_.end();
}

ModuleInfo Context::getModule(std::string_view id) {
    auto it = modules_.find(std::string(id));
    if (it != modules_.end()) {
        return ModuleInfo{it->second->scope, it->second->exports};
    }

    // 惰性创建模块 root scope（GlobalScope 的子作用域，生命周期随作用域树）。
    // 模块有自己的声明记录；外层直接是 GlobalScope（对象记录 + 全局声明记录）。
    void* p = alloc_->alloc(sizeof(Scope));
    if (p == nullptr) return ModuleInfo{};
    Scope* scope = new (p) Scope(global_, alloc_, this);
    global_->addChild(scope);
    // 模块代码恒为严格模式（§16.2.1.1），模块作用域自身标注。
    scope->setFlag("strict");

    // 导出对象必须在模块 scope 下创建，才能被作用域树跟踪。
    // 它是模块命名空间 exotic 对象（原型 null、导出属性为活绑定）。
    Scope* savedCurrent = current_;
    current_ = scope;
    // 模块顶层 this 是 undefined（§16.2.1.6），不能用全局 this。
    Value* moduleThis = createUndefined();
    Value* exports = createModuleNamespace();
    current_ = savedCurrent;
    // P4：createTracked 不再登记匿名值；导出对象由模块 scope 显式锚定，
    // 否则模块注册表（C++ 借用）会在下一次 GC 后悬垂。
    if (exports != oomValue_) scope->add(exports);

    if (moduleThis == oomValue_ || exports == oomValue_) {
        // 创建失败：销毁刚建的作用域（从 global_ 摘除并释放内存）。
        scope->destroySelf();
        return ModuleInfo{};
    }
    scope->setThis(moduleThis);

    auto mod = std::make_unique<Module>();
    mod->id = std::string(id);
    mod->dirname = dirnameOf(id);
    mod->scope = scope;
    mod->exports = exports;
    ModuleInfo info{scope, exports};
    modules_.emplace(std::string(id), std::move(mod));
    return info;
}

void Context::setModuleLoader(std::string_view id, ModuleLoader loader) {
    ModuleInfo info = getModule(id);
    if (info.scope == nullptr) return;
    auto it = modules_.find(std::string(id));
    if (it != modules_.end()) it->second->loader = loader;
}

Module* Context::findModule(std::string_view id) {
    ModuleInfo info = getModule(id);
    if (info.scope == nullptr) return nullptr;
    auto it = modules_.find(std::string(id));
    return it == modules_.end() ? nullptr : it->second.get();
}

bool Context::setModuleCompilation(std::string_view id,
                                   ModuleCompilation compilation) {
    Module* mod = findModule(id);
    if (mod == nullptr) return false;
    // [[HasTLA]] 是编译期静态属性，须在 move 前读取。
    mod->hasTLA = compilation.code != nullptr && compilation.code->hasTopLevelAwait;
    mod->compilation = std::move(compilation);
    mod->declared = false;
    mod->declaring = false;
    mod->instantiated = false;
    mod->instantiating = false;
    mod->loaded = false;
    // 重新编译后求值状态机回到初始态（ADR-064）。
    mod->status = ModuleStatus::Linked;
    mod->dfsAncestorIndex = 0;
    mod->cycleRoot = nullptr;
    mod->asyncEvaluationOrder = kModuleAsyncOrderUnset;
    mod->asyncParentModules.clear();
    mod->pendingAsyncDependencies = 0;
    mod->topLevelPromise = nullptr;
    mod->topLevelResolve = nullptr;
    mod->topLevelReject = nullptr;
    mod->evaluationError = nullptr;
    mod->evaluationCoroutine = nullptr;
    return true;
}

Value* Context::runModuleCode(Module* mod, uint32_t entryPc) {
    if (mod->compilation.code == nullptr) return nullptr;
    Scope* savedRoot = root_;
    Scope* savedCurrent = current_;
    root_ = mod->scope;
    current_ = mod->scope;
    vm::VM machine(this, mod->compilation.code.get());
    Value* res = machine.run(entryPc);
    root_ = savedRoot;
    current_ = savedCurrent;
    return res;
}

Value* Context::declareModule(Module* mod) {
    if (mod->declared) return nullptr;
    // 循环依赖：依赖方槽位尚未建立，先返回让其继续声明。
    if (mod->declaring) return nullptr;

    mod->declaring = true;
    moduleStack_.push_back(mod);

    Scope* savedRoot = root_;
    Scope* savedCurrent = current_;
    root_ = mod->scope;
    current_ = mod->scope;
    auto fail = [&](Value* error) -> Value* {
        moduleStack_.pop_back();
        mod->declaring = false;
        root_ = savedRoot;
        current_ = savedCurrent;
        return error;
    };

    // 1. 符号定义区：建槽。
    Value* res = runModuleCode(mod, mod->compilation.declarePc);
    if (res != nullptr && detail::isErrorValue(res)) return fail(res);

    // 2. 本地绑定导出：槽位已存在，先建立映射（循环依赖时依赖方即可解析）。
    //    导出名指向 import 别名的条目此时还无法解析，留到 instantiate 重试。
    for (const ModuleExportBinding& exp : mod->compilation.exports) {
        if (!exp.specifier.empty() || exp.exportAll) continue;
        if (current()->lookupBindingChain(exp.localName) == nullptr) continue;
        Value* exportRes =
            exportBinding(mod->exports, exp.exportName, exp.localName);
        if (detail::isErrorValue(exportRes)) return fail(exportRes);
    }

    // 3. 递归声明依赖（import 与 re-export 的源模块）。
    for (const ModuleImportBinding& imp : mod->compilation.imports) {
        Module* dep = findModule(resolveSpecifier(moduleStack_, imp.specifier));
        if (dep == nullptr) return fail(oomValue_);
        Value* depRes = declareModule(dep);
        if (depRes != nullptr && detail::isErrorValue(depRes)) {
            return fail(depRes);
        }
    }
    for (const ModuleExportBinding& exp : mod->compilation.exports) {
        if (exp.specifier.empty()) continue;
        Module* dep = findModule(resolveSpecifier(moduleStack_, exp.specifier));
        if (dep == nullptr) return fail(oomValue_);
        Value* depRes = declareModule(dep);
        if (depRes != nullptr && detail::isErrorValue(depRes)) {
            return fail(depRes);
        }
    }

    moduleStack_.pop_back();
    mod->declaring = false;
    root_ = savedRoot;
    current_ = savedCurrent;
    mod->declared = true;
    return nullptr;
}

Value* Context::instantiateModule(Module* mod) {
    if (mod->instantiated) return nullptr;
    // 循环依赖：依赖方的 import/export 尚未建立，先返回让其继续实例化。
    if (mod->instantiating) return nullptr;

    Value* declared = declareModule(mod);
    if (declared != nullptr && detail::isErrorValue(declared)) return declared;

    mod->instantiating = true;
    moduleStack_.push_back(mod);

    // link 期需要 current 指向模块作用域（import 绑定与 export 槽位查找）。
    Scope* savedRoot = root_;
    Scope* savedCurrent = current_;
    root_ = mod->scope;
    current_ = mod->scope;
    auto fail = [&](Value* error) -> Value* {
        moduleStack_.pop_back();
        mod->instantiating = false;
        root_ = savedRoot;
        current_ = savedCurrent;
        return error;
    };

    // 1. import：依赖已在 declare 阶段建好本地导出槽位，这里建立共享别名。
    for (const ModuleImportBinding& imp : mod->compilation.imports) {
        std::string depId = resolveSpecifier(moduleStack_, imp.specifier);
        Module* dep = findModule(depId);
        if (dep == nullptr) return fail(oomValue_);
        Value* depRes = instantiateModule(dep);
        if (depRes != nullptr && detail::isErrorValue(depRes)) {
            return fail(depRes);
        }
        Value* bindRes =
            imp.namespaceImport
                ? bindImportSlot(imp.localName, dep->exports)
                : bindImport(imp.localName, dep->exports, imp.importName);
        if (bindRes != nullptr && detail::isErrorValue(bindRes)) {
            return fail(bindRes);
        }
    }

    // 2. 本地导出重试：声明阶段无法解析的 import 别名现在可以建立。
    for (const ModuleExportBinding& exp : mod->compilation.exports) {
        if (!exp.specifier.empty() || exp.exportAll) continue;
        if (findExportSlot(mod->exports, exp.exportName) != nullptr) continue;
        Value* exportRes =
            exportBinding(mod->exports, exp.exportName, exp.localName);
        if (detail::isErrorValue(exportRes)) return fail(exportRes);
    }

    // 3. re-export / export *：依赖的导出表此时已建立。
    for (const ModuleExportBinding& exp : mod->compilation.exports) {
        if (exp.exportAll) {
            std::string depId = resolveSpecifier(moduleStack_, exp.specifier);
            Module* dep = findModule(depId);
            if (dep == nullptr) return fail(oomValue_);
            Value* depRes = instantiateModule(dep);
            if (depRes != nullptr && detail::isErrorValue(depRes)) {
                return fail(depRes);
            }
            Value* keys = ownPropertyKeys(dep->exports, true, false);
            if (detail::isErrorValue(keys)) return fail(keys);
            auto* elements = static_cast<ArrayData*>(keys->data());
            for (uint32_t i = 0; i < elements->length(); ++i) {
                Value* key = elements->elementAt(i);
                if (key == nullptr || key->type()->kind() != TypeKind::String) {
                    continue;
                }
                std::string name =
                    static_cast<StringData*>(key->data())->value;
                if (name == "default") continue;
                Value* slot = findExportSlot(dep->exports, name);
                if (slot == nullptr) continue;
                Value* exportRes = exportSlot(mod->exports, name, slot);
                if (detail::isErrorValue(exportRes)) return fail(exportRes);
            }
            continue;
        }

        if (exp.specifier.empty()) continue;
        std::string depId = resolveSpecifier(moduleStack_, exp.specifier);
        Module* dep = findModule(depId);
        if (dep == nullptr) return fail(oomValue_);
        Value* depRes = instantiateModule(dep);
        if (depRes != nullptr && detail::isErrorValue(depRes)) {
            return fail(depRes);
        }
        // importName 为空：export * as ns，导出命名空间对象本身。
        Value* slot = exp.importName.empty()
                          ? dep->exports
                          : getExportSlot(dep->exports, exp.importName);
        if (detail::isErrorValue(slot)) return fail(slot);
        Value* exportRes =
            exportSlot(mod->exports, exp.exportName, slot);
        if (detail::isErrorValue(exportRes)) return fail(exportRes);
    }

    // 4. 函数定义区：import 已绑定，set_closure 可解析。
    Value* res = runModuleCode(mod, mod->compilation.instantiatePc);
    if (res != nullptr && detail::isErrorValue(res)) return fail(res);

    moduleStack_.pop_back();
    mod->instantiating = false;
    root_ = savedRoot;
    current_ = savedCurrent;
    mod->instantiated = true;
    return nullptr;
}

Value* Context::createModuleCoroutine(Module* mod) {
    auto* d = gc_->allocData<CoroutineData>();
    if (d == nullptr) return oomValue_;
    d->state = CoroutineState::Suspended;
    d->receiver = createUndefined();
    if (d->receiver == oomValue_) return d->receiver;
    d->yielded = createUndefined();
    if (d->yielded == oomValue_) return d->yielded;
    d->sentValue = createUndefined();
    if (d->sentValue == oomValue_) return d->sentValue;
    d->module = mod;
    d->resume = &Context::resumeModuleCoroutine;

    Value* co = createTracked(getCoroutineType(), d, {});
    if (co == oomValue_) return co;
    if (objectPrototype_ != nullptr) {
        static_cast<ObjectData*>(co->data())->prototype = objectPrototype_;
    }
    return co;
}

Value* Context::resumeModuleCoroutine(IContext* ctx, Value* coroutine,
                                      Value* sentValue) {
    auto* co = asCoroutine(coroutine);
    if (co == nullptr || co->module == nullptr) {
        return detail::typeError(ctx, "module coroutine has no module");
    }
    Module* mod = co->module;
    if (mod->compilation.code == nullptr) {
        return detail::typeError(ctx, "module has no compiled code");
    }
    auto* self = static_cast<Context*>(ctx);

    Scope* savedRoot = ctx->root();
    Scope* savedCurrent = ctx->current();
    Scope* savedFunctionEnv = ctx->currentFunctionEnv();
    Value* savedFunction = ctx->currentFunction();

    ctx->setRootScope(mod->scope);
    ctx->setCurrentScope(mod->scope);
    self->moduleStack_.push_back(mod);

    auto restore = [&]() {
        self->moduleStack_.pop_back();
        ctx->setCurrentFunctionEnv(savedFunctionEnv);
        ctx->setCurrentFunction(savedFunction);
        ctx->setCurrentScope(savedCurrent);
        ctx->setRootScope(savedRoot);
    };

    vm::VM machine(ctx, mod->compilation.code.get());
    Value* res = nullptr;
    if (!co->started) {
        co->started = true;
        res = machine.run(mod->compilation.evaluatePc);
    } else {
        // await 拒绝以 Exception 注入，转为挂起点的 throw。
        Value* injected = sentValue;
        if (sentValue != nullptr &&
            sentValue->type()->kind() == TypeKind::Exception) {
            injected = ctx->createInterrupt(InterruptKind::Throw, 0, sentValue);
        }
        res = machine.resume(*co->vmState, injected);
    }

    const bool suspended =
        res != nullptr && detail::isInterruptValue(res) &&
        isSuspensionSignal(asInterrupt(res)->kind);
    if (suspended) {
        if (co->vmState == nullptr) co->vmState = new vm::VmState();
        *co->vmState = machine.snapshot();
        co->yielded = asInterrupt(res)->value;
        co->state = CoroutineState::Suspended;
        restore();
        return res;
    }

    if (res == nullptr) res = ctx->createUndefined();
    co->state = CoroutineState::Completed;
    co->yielded = res;
    delete co->vmState;
    co->vmState = nullptr;
    // 完成/失败由 AsyncModuleExecutionFulfilled/Rejected 统一处理。
    restore();
    return res;
}

Value* Context::ensureTopLevelCapability(Module* mod) {
    if (mod->topLevelPromise != nullptr) return mod->topLevelPromise;
    Value* ctor = promiseCtor();
    if (ctor == nullptr) {
        return detail::typeError(this, "Promise is not initialized");
    }
    PromiseCapability cap;
    if (Value* err = createPromiseCapability(this, ctor, cap)) return err;
    mod->topLevelPromise = cap.promise;
    mod->topLevelResolve = cap.resolve;
    mod->topLevelReject = cap.reject;
    // Module 字段不是 GC 根，显式登记到模块作用域。
    mod->scope->add(cap.promise);
    mod->scope->add(cap.resolve);
    mod->scope->add(cap.reject);
    return cap.promise;
}

void Context::resolveTopLevelCapability(Module* mod) {
    if (mod->topLevelResolve == nullptr) return;
    settle(this, mod->topLevelResolve, createUndefined());
}

void Context::rejectTopLevelCapability(Module* mod, Value* error) {
    if (mod->topLevelReject == nullptr) return;
    settle(this, mod->topLevelReject, reasonOf(error));
}

Value* Context::executeModuleSync(Module* mod) {
    moduleStack_.push_back(mod);
    Value* res = runModuleCode(mod, mod->compilation.evaluatePc);
    moduleStack_.pop_back();
    if (res != nullptr && detail::isErrorValue(res)) {
        mod->evaluationError = res;
        mod->scope->add(res);
        return res;
    }
    return nullptr;
}

void Context::executeAsyncModule(Module* mod) {
    // ExecuteAsyncModule step 1：进入 EVALUATING-ASYNC，依赖方据此登记异步父模块。
    mod->status = ModuleStatus::EvaluatingAsync;
    Value* co = mod->evaluationCoroutine;
    if (co == nullptr) {
        co = createModuleCoroutine(mod);
        if (detail::isErrorValue(co)) {
            asyncModuleExecutionRejected(mod, co);
            return;
        }
        mod->evaluationCoroutine = co;
        mod->scope->add(co);
    }

    Value* onFulfilled = createNativeFunction(moduleAsyncFulfilled, 0);
    Value* onRejected = createNativeFunction(moduleAsyncRejected, 1);
    Value* moduleId = createString(mod->id);
    if (onFulfilled == oomValue_ || onRejected == oomValue_ ||
        moduleId == oomValue_) {
        asyncModuleExecutionRejected(mod, oomValue_);
        return;
    }
    captureValue(onFulfilled, "__modId", moduleId);
    captureValue(onRejected, "__modId", moduleId);

    Value* res = resumeCoroutine(co, nullptr);
    driveAsyncCoroutine(this, co, res, onFulfilled, onRejected);
}

void Context::gatherAvailableAncestors(Module* mod,
                                       std::vector<Module*>& execList) {
    for (Module* parent : mod->asyncParentModules) {
        if (std::find(execList.begin(), execList.end(), parent) !=
            execList.end()) {
            continue;
        }
        if (parent->cycleRoot != nullptr &&
            parent->cycleRoot->evaluationError != nullptr) {
            continue;
        }
        if (parent->status != ModuleStatus::EvaluatingAsync ||
            parent->pendingAsyncDependencies == 0) {
            continue;
        }
        --parent->pendingAsyncDependencies;
        if (parent->pendingAsyncDependencies == 0) {
            execList.push_back(parent);
            if (!parent->hasTLA) gatherAvailableAncestors(parent, execList);
        }
    }
}

void Context::asyncModuleExecutionFulfilled(Module* mod) {
    if (mod->status == ModuleStatus::Evaluated) return;
    mod->asyncEvaluationOrder = kModuleAsyncOrderDone;
    mod->status = ModuleStatus::Evaluated;
    if (mod->cycleRoot == mod) resolveTopLevelCapability(mod);

    std::vector<Module*> execList;
    gatherAvailableAncestors(mod, execList);
    std::sort(execList.begin(), execList.end(),
              [](const Module* a, const Module* b) {
                  return a->asyncEvaluationOrder < b->asyncEvaluationOrder;
              });

    for (Module* m : execList) {
        if (m->status == ModuleStatus::Evaluated) continue;
        if (m->hasTLA) {
            executeAsyncModule(m);
            continue;
        }
        Value* err = executeModuleSync(m);
        if (err != nullptr) {
            asyncModuleExecutionRejected(m, err);
            continue;
        }
        m->asyncEvaluationOrder = kModuleAsyncOrderDone;
        m->status = ModuleStatus::Evaluated;
        if (m->cycleRoot == m) resolveTopLevelCapability(m);
    }
}

void Context::asyncModuleExecutionRejected(Module* mod, Value* error) {
    if (mod->status == ModuleStatus::Evaluated) return;
    mod->evaluationError = error;
    mod->scope->add(error);
    mod->asyncEvaluationOrder = kModuleAsyncOrderDone;
    mod->status = ModuleStatus::Evaluated;
    if (mod->cycleRoot == mod) rejectTopLevelCapability(mod, error);
    for (Module* parent : mod->asyncParentModules) {
        asyncModuleExecutionRejected(parent, error);
    }
}

Value* Context::innerModuleEvaluation(Module* mod, std::vector<Module*>& stack,
                                      size_t& index) {
    // 原生 loader 模块：同步执行，不参与异步图。
    if (mod->compilation.code == nullptr) {
        Value* res = importModule(mod->id);
        return (res != nullptr && detail::isErrorValue(res)) ? res : nullptr;
    }

    if (mod->status == ModuleStatus::EvaluatingAsync ||
        mod->status == ModuleStatus::Evaluated) {
        return mod->evaluationError;
    }
    if (mod->status == ModuleStatus::Evaluating) return nullptr;

    mod->status = ModuleStatus::Evaluating;
    const size_t moduleIndex = index;
    mod->dfsAncestorIndex = index;
    mod->pendingAsyncDependencies = 0;
    ++index;
    stack.push_back(mod);

    std::unordered_set<Module*> seen;
    moduleStack_.push_back(mod);
    auto fail = [&](Value* error) -> Value* {
        moduleStack_.pop_back();
        return error;
    };

    auto visit = [&](const std::string& specifier) -> Value* {
        std::string depId = resolveSpecifier(moduleStack_, specifier);
        Module* dep = findModule(depId);
        if (dep == nullptr) return oomValue_;
        if (!seen.insert(dep).second) return nullptr;
        if (dep->compilation.code != nullptr) {
            Value* err = instantiateModule(dep);
            if (err != nullptr && detail::isErrorValue(err)) return err;
        }
        Value* err = innerModuleEvaluation(dep, stack, index);
        if (err != nullptr && detail::isErrorValue(err)) return err;
        if (dep->compilation.code == nullptr) return nullptr;

        if (dep->status == ModuleStatus::Evaluating) {
            mod->dfsAncestorIndex =
                std::min(mod->dfsAncestorIndex, dep->dfsAncestorIndex);
            return nullptr;
        }
        Module* root = dep->cycleRoot != nullptr ? dep->cycleRoot : dep;
        if (root->evaluationError != nullptr) return root->evaluationError;
        dep = root;
        // 异步依赖：登记父模块并计数（§16.2.1.6.1.7 step 11.v）。
        if (dep->asyncEvaluationOrder != kModuleAsyncOrderUnset &&
            dep->asyncEvaluationOrder != kModuleAsyncOrderDone) {
            ++mod->pendingAsyncDependencies;
            dep->asyncParentModules.push_back(mod);
        }
        return nullptr;
    };

    for (const ModuleImportBinding& imp : mod->compilation.imports) {
        Value* err = visit(imp.specifier);
        if (err != nullptr) return fail(err);
    }
    for (const ModuleExportBinding& exp : mod->compilation.exports) {
        if (exp.specifier.empty()) continue;
        Value* err = visit(exp.specifier);
        if (err != nullptr) return fail(err);
    }
    moduleStack_.pop_back();

    if (mod->pendingAsyncDependencies > 0 || mod->hasTLA) {
        mod->asyncEvaluationOrder = ++moduleAsyncEvaluationCount_;
        if (mod->pendingAsyncDependencies == 0) executeAsyncModule(mod);
    } else {
        Value* err = executeModuleSync(mod);
        if (err != nullptr) return err;
    }

    // SCC 展开：栈上直到本模块的成员共享 cycleRoot（§16.2.1.6.1.7 step 16）。
    if (mod->dfsAncestorIndex == moduleIndex) {
        while (!stack.empty()) {
            Module* required = stack.back();
            stack.pop_back();
            required->status =
                required->asyncEvaluationOrder == kModuleAsyncOrderUnset
                    ? ModuleStatus::Evaluated
                    : ModuleStatus::EvaluatingAsync;
            required->cycleRoot = mod;
            if (required == mod) break;
        }
    }
    return nullptr;
}

Value* Context::evaluateModule(Module* mod) {
    if (mod->status == ModuleStatus::Evaluated) return mod->evaluationError;
    if (mod->status == ModuleStatus::EvaluatingAsync) {
        Module* root = mod->cycleRoot != nullptr ? mod->cycleRoot : mod;
        if (root->status == ModuleStatus::Evaluated) return root->evaluationError;
        return ensureTopLevelCapability(root);
    }

    Value* promise = ensureTopLevelCapability(mod);
    if (detail::isErrorValue(promise)) return promise;

    std::vector<Module*> stack;
    size_t index = 0;
    Value* res = innerModuleEvaluation(mod, stack, index);
    if (res != nullptr && detail::isErrorValue(res)) {
        for (Module* m : stack) {
            m->status = ModuleStatus::Evaluated;
            m->evaluationError = res;
            m->scope->add(res);
        }
        rejectTopLevelCapability(mod, res);
        return res;
    }
    if (mod->status == ModuleStatus::Evaluated) {
        if (mod->evaluationError != nullptr) {
            rejectTopLevelCapability(mod, mod->evaluationError);
            return mod->evaluationError;
        }
        resolveTopLevelCapability(mod);
        return nullptr;
    }
    return promise;
}

Value* Context::importModule(std::string_view specifier) {
    // 相对说明符以「当前正在加载的模块」的目录为基准解析。
    std::string id = resolveSpecifier(moduleStack_, specifier);
    ModuleInfo info = getModule(id);
    if (info.scope == nullptr) return oomValue_;

    Module* mod = modules_.find(id)->second.get();

    // 编译模块：实例化 → 链接 → 求值（循环依赖由求值状态机短路）。
    if (mod->compilation.code != nullptr) {
        if (mod->status == ModuleStatus::Evaluated) {
            return mod->evaluationError != nullptr ? mod->evaluationError
                                                   : mod->exports;
        }
        Value* err = instantiateModule(mod);
        if (err != nullptr && detail::isErrorValue(err)) return err;
        err = evaluateModule(mod);
        if (err != nullptr && detail::isErrorValue(err)) return err;
        return mod->exports;
    }

    // 已加载 / 正在加载（循环依赖）/ 无 loader：直接返回当前导出对象。
    if (mod->loaded || mod->loading || mod->loader == nullptr) {
        return mod->exports;
    }

    mod->loading = true;
    Scope* savedRoot = root_;
    Scope* savedCurrent = current_;
    root_ = mod->scope;
    current_ = mod->scope;
    moduleStack_.push_back(mod);
    Value* result = mod->loader(this, mod->exports);
    moduleStack_.pop_back();
    root_ = savedRoot;
    current_ = savedCurrent;
    mod->loading = false;

    // 失败：保持未加载（允许重试），原样透传错误值。
    if (result != nullptr && detail::isErrorValue(result)) return result;
    mod->loaded = true;
    return mod->exports;
}

Value* Context::importModuleDynamic(Value* specifier,
                                    std::string_view referrer) {
    Value* promise = createPromise();
    if (promise == oomValue_) return promise;

    // §13.3.10.1：specifier 先 ToString；失败则 reject。
    Value* str = toString(specifier);
    if (str == oomValue_) {
        promiseReject(this, promise, str);
        return promise;
    }
    if (detail::isErrorValue(str)) {
        promiseReject(this, promise,
                      str->type()->kind() == TypeKind::Exception
                          ? static_cast<ExceptionData*>(str->data())->child
                          : str);
        return promise;
    }

    std::string id = static_cast<StringData*>(str->data())->value;
    if (isRelativeSpecifier(id) && !referrer.empty()) {
        std::string joined = dirnameOf(std::string(referrer));
        if (!joined.empty()) joined.push_back('/');
        joined += id;
        id = normalizePath(joined);
    }

    Value* ns = importModule(id);
    if (ns != nullptr && detail::isErrorValue(ns)) {
        promiseReject(this, promise,
                      ns->type()->kind() == TypeKind::Exception
                          ? static_cast<ExceptionData*>(ns->data())->child
                          : ns);
        return promise;
    }
    Value* value = ns != nullptr ? ns : createUndefined();
    if (value == oomValue_) {
        promiseReject(this, promise, value);
        return promise;
    }

    // 顶层 await：模块求值尚未完成时，等其（环根的）求值 Promise settle
    // 后再交付命名空间（ADR-064）。
    Module* mod = findModule(id);
    if (mod != nullptr && mod->compilation.code != nullptr) {
        Value* evalPromise = nullptr;
        if (mod->status == ModuleStatus::Evaluated) {
            if (mod->evaluationError != nullptr) {
                promiseReject(this, promise, reasonOf(mod->evaluationError));
                return promise;
            }
        } else {
            evalPromise = evaluateModule(mod);
            if (detail::isErrorValue(evalPromise)) {
                promiseReject(this, promise, reasonOf(evalPromise));
                return promise;
            }
        }
        if (evalPromise != nullptr) {
            Value* thenFn = getProperty(evalPromise, "then");
            if (detail::isErrorValue(thenFn)) {
                promiseReject(this, promise, reasonOf(thenFn));
                return promise;
            }
            Value* onFulfilled = createNativeFunction(dynamicImportFulfilled, 1);
            Value* onRejected = createNativeFunction(dynamicImportRejected, 1);
            if (onFulfilled == oomValue_ || onRejected == oomValue_) {
                promiseReject(this, promise, oomValue_);
                return promise;
            }
            captureValue(onFulfilled, "__promise", promise);
            captureValue(onFulfilled, "__ns", value);
            captureValue(onRejected, "__promise", promise);
            Value* args[2] = {onFulfilled, onRejected};
            Value* thenRes = call(thenFn, evalPromise, 2, args);
            if (detail::isErrorValue(thenRes)) {
                promiseReject(this, promise, reasonOf(thenRes));
            }
            return promise;
        }
    }

    promiseFulfill(this, promise, value);
    return promise;
}

Value* Context::importMeta() {
    if (moduleStack_.empty()) return createUndefined();
    Module* mod = moduleStack_.back();
    if (mod->importMeta != nullptr) return mod->importMeta;

    // 在模块 root scope 下创建，保证对象从 global 作用域树可达（ADR-044）。
    Scope* savedCurrent = current_;
    current_ = mod->scope;
    Value* meta = createObject();
    if (meta == oomValue_) {
        current_ = savedCurrent;
        return meta;
    }
    // P4：import.meta 对象由模块 scope 显式锚定（Module::importMeta 为借用）。
    mod->scope->add(meta);
    Value* url = createString(mod->id);
    if (url == oomValue_) {
        current_ = savedCurrent;
        return url;
    }
    Value* result = setProperty(meta, "url", url);
    current_ = savedCurrent;
    if (detail::isErrorValue(result)) return result;
    mod->importMeta = meta;
    return meta;
}

Value* Context::exportSlot(Value* exports, std::string_view exportName,
                           Value* slot) {
    if (detail::isErrorValue(exports)) return exports;
    if (exports->type()->kind() != TypeKind::ModuleNamespace) {
        return detail::typeError(this, "export target is not a module namespace");
    }
    if (slot == nullptr) {
        return detail::referenceError(
            this, "export '" + std::string(exportName) + "' has no binding");
    }
    // ModuleNamespaceType::defineProperty 直接存槽位 Value*，不 clone。
    return defineProperty(exports, exportName, slot, false, true, false);
}

Value* Context::getExportSlot(Value* exports, std::string_view exportName) {
    if (detail::isErrorValue(exports)) return exports;
    Value* slot = findExportSlot(exports, exportName);
    if (slot == nullptr) {
        return detail::referenceError(
            this, "export '" + std::string(exportName) + "' is not defined");
    }
    return slot;
}

Value* Context::bindImportSlot(std::string_view localName, Value* slot) {
    if (detail::isErrorValue(slot)) return slot;
    if (slot == nullptr) {
        return detail::referenceError(
            this, "import binding '" + std::string(localName) + "' has no slot");
    }
    if (!current()->defineImport(localName, slot)) {
        return detail::errorOf(this, ErrorKind::SyntaxError,
                               "Identifier '" + std::string(localName) +
                                   "' has already been declared");
    }
    // 返回槽位当前值；未初始化时返回 undefined（读取仍由 load 抛 TDZ）。
    return slot->type()->kind() == TypeKind::Uninitialized ? createUndefined()
                                                           : slot;
}

Value* Context::exportBinding(Value* exports, std::string_view exportName,
                              std::string_view localName) {
    if (detail::isErrorValue(exports)) return exports;
    if (exports->type()->kind() != TypeKind::ModuleNamespace) {
        return detail::typeError(this, "export target is not a module namespace");
    }
    // re-export：localName 是导入绑定时，导出直接指向同一个源槽位。
    Value* slot = nullptr;
    if (!current()->getImportBindingChain(localName, slot)) {
        // 本地导出：直接取模块作用域里的绑定槽位。
        const Scope::Binding* binding =
            current()->lookupBindingChain(localName);
        if (binding != nullptr) slot = binding->value;
    }
    if (slot == nullptr) {
        return detail::referenceError(
            this, "export '" + std::string(localName) + "' is not defined");
    }
    return exportSlot(exports, exportName, slot);
}

Value* Context::bindImport(std::string_view localName, Value* namespaceObj,
                           std::string_view exportName) {
    if (detail::isErrorValue(namespaceObj)) return namespaceObj;
    if (namespaceObj->type()->kind() != TypeKind::ModuleNamespace) {
        return detail::typeError(this, "import target is not a module namespace");
    }
    Value* slot = findExportSlot(namespaceObj, exportName);
    if (slot == nullptr) {
        return detail::referenceError(
            this, "export '" + std::string(exportName) + "' is not defined");
    }
    return bindImportSlot(localName, slot);
}

}  // namespace neo::core
