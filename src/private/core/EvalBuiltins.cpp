// EvalBuiltins.cpp - 全局 eval 内建与运行时编译入口（ES2024 §19.2.1）
//
// 直接 eval：Compiler 在 `eval(...)` 调用点发射 DirectEval；VM 确认被调函数
// 是内建 %eval% 后，把调用方当前作用域作为词法环境交给 evalSource。
// 间接 eval：普通调用内建函数，在 global 作用域执行。
//
// 作用域模型：
//   - 词法环境：直接 eval 取调用方 current，间接 eval 取 global；
//   - 变量环境：sloppy 直接 eval 取调用方函数环境（var/函数声明泄漏到调用方），
//     其余取 eval 自己的词法作用域（严格 eval 的 var 不外泄）。
// 编译产物入口先 pushScope 建 eval 词法作用域，evalSource 在返回值归属调用方
// 后统一 pop 到入口作用域（异常路径同样收口）。
//
// 关联 ADR: 006（异常即值）、033（eval 与隔离函数）、058（Sema）、
//          060（VM 框架）、061（Compiler 框架）

#include "core/EvalBuiltins.h"

#include "core/Context.h"
#include "core/Scope.h"
#include "core/types/Function.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "frontend/Node.h"
#include "frontend/Parser.h"
#include "compiler/Compiler.h"
#include "sema/Sema.h"
#include "vm/VM.h"

#include <string>

namespace neo::core {

namespace {

// 指令序言中是否含 "use strict"（只检查开头的 Directive 序列）。
bool hasUseStrictDirective(const frontend::NodeList& body) {
    for (const frontend::NodePtr& statement : body) {
        if (!statement || statement->kind() != frontend::NodeKind::Directive) {
            break;
        }
        if (static_cast<const frontend::Directive&>(*statement).isUseStrict()) {
            return true;
        }
    }
    return false;
}

// 间接 eval：eval(x) 非字符串原样返回；无参返回 undefined。
Value* evalNative(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc == 0) return ctx->createUndefined();
    return ctx->evalSource(argv[0], nullptr, false);
}

}  // namespace

Value* Context::evalSource(Value* source, Scope* callerScope, bool direct) {
    if (detail::isErrorValue(source)) return source;
    // §19.2.1：非 String 直接返回参数本身。
    if (source->type()->kind() != TypeKind::String) return source;

    const std::string& text = static_cast<StringData*>(source->data())->value;

    // 1. 解析为 Eval（Script 语义，但 Sema 需继承调用方上下文）。
    frontend::Parser parser(text,
                            frontend::ParserOptions{frontend::SourceType::Eval});
    frontend::ParseResult parsed = parser.parseProgram();
    if (!parsed.ok()) {
        std::string message = !parsed.errors.empty()
                                  ? parsed.errors.front().message
                                  : "Invalid eval source";
        return createErrorException(ErrorKind::SyntaxError, message);
    }

    // 2. 严格模式：直接 eval 继承调用方；源码自身 "use strict" 同样生效。
    const bool inheritedStrict =
        direct && callerScope != nullptr && callerScope->lookupFlagChain("strict");
    const auto& program =
        static_cast<const frontend::Program&>(*parsed.program);
    const bool strict =
        inheritedStrict || hasUseStrictDirective(program.body);

    // 运行时 Context → Sema 上下文快照（§19.2.1.1 step 10/11）。
    sema::SemaOptions semaOptions;
    semaOptions.inheritedStrict = strict;
    semaOptions.evalContext = true;
    if (direct && currentFunction_ != nullptr &&
        currentFunction_->type()->kind() == TypeKind::Function) {
        auto* d = static_cast<FunctionData*>(currentFunction_->data());
        semaOptions.allowsNewTarget = true;
        bool baseConstructor = false;
        if (d->isClassConstructor) {
            auto* cd = static_cast<ClassData*>(d);
            semaOptions.allowsSuperCall = cd->isDerived;
            baseConstructor = !cd->isDerived;
        }
        // HasSuperBinding：有 [[HomeObject]] 且不是基类构造器。
        semaOptions.allowsSuperProperty =
            d->homeObject != nullptr && !baseConstructor;
    }

    sema::SemaResult sema =
        sema::runSema(*parsed.program, semaOptions);
    if (!sema.ok()) {
        std::string message = !sema.errors.empty() ? sema.errors.front().message
                                                   : "Invalid eval source";
        return createErrorException(ErrorKind::SyntaxError, message);
    }

    compiler::CompilerOptions options;
    options.filename = "<eval>";
    options.eval = true;
    options.evalStrict = strict;

    // 直接 eval 继承调用方 PrivateEnvironment（§19.2.1.1 step 10）：
    // 从当前函数的 ownerClass 还原可见私有名及其声明种类。
    if (direct && currentFunction_ != nullptr &&
        currentFunction_->type()->kind() == TypeKind::Function) {
        auto* fd = static_cast<FunctionData*>(currentFunction_->data());
        if (fd->ownerClass != nullptr &&
            fd->ownerClass->type()->kind() == TypeKind::Function) {
            auto* cd = static_cast<ClassData*>(fd->ownerClass->data());
            for (const InstanceElement& e : cd->instanceElements) {
                if (e.kind != InstanceElement::Kind::PrivateField ||
                    e.name == nullptr) {
                    continue;
                }
                options.privateEnvironment[e.name->description].field = true;
            }
            for (const PrivateFieldDef& f : cd->staticPrivateFields) {
                if (f.name != nullptr) {
                    options.privateEnvironment[f.name->description].field = true;
                }
            }
            for (const PrivateElement& e : cd->privateElements) {
                if (e.name == nullptr) continue;
                compiler::PrivateNameInfo& info =
                    options.privateEnvironment[e.name->description];
                if (e.kind == PrivateElement::Kind::Method) {
                    info.method = true;
                } else {
                    info.getter = e.getter != nullptr;
                    info.setter = e.setter != nullptr;
                }
            }
            for (const PrivateElement& e : cd->staticPrivateElements) {
                if (e.name == nullptr) continue;
                compiler::PrivateNameInfo& info =
                    options.privateEnvironment[e.name->description];
                if (e.kind == PrivateElement::Kind::Method) {
                    info.method = true;
                } else {
                    info.getter = e.getter != nullptr;
                    info.setter = e.setter != nullptr;
                }
            }
        }
    }
    compiler::Compiler compiler(
        static_cast<const frontend::Program&>(*parsed.program), sema, options);
    compiler::CompileResult compiled = compiler.compile();
    if (!compiled.ok()) {
        std::string message = !compiled.errors.empty()
                                  ? compiled.errors.front().message
                                  : "Invalid eval source";
        return createErrorException(ErrorKind::SyntaxError, message);
    }

    // 3. 在目标作用域执行。var 环境按严格性分派：
    //    sloppy 直接 eval → 调用方变量环境；其余 → eval 自己的词法作用域。
    Scope* entry = direct && callerScope != nullptr ? callerScope : global_;
    Scope* savedCurrent = current_;
    Scope* savedRoot = root_;
    Scope* savedFunctionEnv = currentFunctionEnv_;
    Scope* savedEvalVar = evalVarScope_;
    Value* savedFunction = currentFunction_;

    evalVarScope_ = nullptr;
    if (!strict) {
        evalVarScope_ = direct ? (currentFunctionEnv_ != nullptr
                                      ? currentFunctionEnv_
                                      : root_)
                               : global_;
    }
    if (direct) entry->markDynamic();
    current_ = entry;
    // 间接 eval 在全局环境中执行：新建函数的模块根取 global。
    if (!direct) root_ = global_;

    // eval 内创建的函数可能逃逸并长期引用 Code，故 Code 由 Context 持有。
    vm::Code* code = compiled.code.release();
    evalCodes_.emplace_back(code);

    vm::VM machine(this, code);
    Value* result = machine.run();

    // 4. 返回值归属调用方，再收口 eval 词法作用域（异常路径同样清理）。
    if (result != nullptr) savedCurrent->add(result);
    while (current_ != entry) popScope();

    evalVarScope_ = savedEvalVar;
    current_ = savedCurrent;
    root_ = savedRoot;
    currentFunctionEnv_ = savedFunctionEnv;
    currentFunction_ = savedFunction;
    return result;
}

bool registerEvalBuiltin(IContext* ctx) {
    // %eval% 是普通函数，不可构造、无 prototype（§19.2.1）。
    FunctionCreateOptions options;
    options.constructible = false;
    options.hasPrototypeProperty = false;
    Value* fn = ctx->createNativeFunction(evalNative, 1, {}, options);
    if (fn == ctx->oomValue()) return false;
    ctx->setFunctionName(fn, "eval");
    ctx->global()->add(fn);
    ctx->global()->bind("eval", fn);
    ctx->setEvalFunction(fn);
    return true;
}

}  // namespace neo::core
