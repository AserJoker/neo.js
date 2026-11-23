// TestRunner.cpp - Test262 单用例执行实现

#include "TestRunner.h"

#include "Frontmatter.h"

#include "compiler/Compiler.h"
#include "core/ErrorKind.h"
#include "core/IContext.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/ArrayBuffer.h"
#include "core/types/Exception.h"
#include "core/types/Fatal.h"
#include "core/types/Interrupt.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "frontend/Node.h"
#include "frontend/Parser.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/IOutputSink.h"
#include "infra/MallocAllocator.h"
#include "sema/Sema.h"
#include "vm/VM.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace neo::test262 {

namespace {

using neo::core::ExceptionData;
using neo::core::FatalData;
using neo::core::IContext;
using neo::core::InterruptData;
using neo::core::StringData;
using neo::core::TypeKind;
using neo::core::Value;

class NullSink final : public neo::infra::IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string stringOf(Value* value) {
    if (value == nullptr || value->type()->kind() != TypeKind::String) return {};
    return static_cast<StringData*>(value->data())->value;
}

std::string errorNameOf(IContext* ctx, Value* error) {
    if (error == nullptr) return {};
    switch (error->type()->kind()) {
        case TypeKind::Fatal:
            return "Fatal";
        case TypeKind::Interrupt: {
            const auto* data = static_cast<InterruptData*>(error->data());
            if (data->kind == neo::core::InterruptKind::Terminate) return "Terminate";
            return "Interrupt";
        }
        case TypeKind::Exception: {
            Value* child = static_cast<ExceptionData*>(error->data())->child;
            std::string name = stringOf(ctx->getProperty(child, "name"));
            return name.empty() ? "Error" : name;
        }
        default:
            return {};
    }
}

std::string errorMessageOf(IContext* ctx, Value* error) {
    if (error == nullptr) return {};
    switch (error->type()->kind()) {
        case TypeKind::Fatal:
            return static_cast<FatalData*>(error->data())->message;
        case TypeKind::Interrupt:
            return "execution interrupted";
        case TypeKind::Exception: {
            Value* child = static_cast<ExceptionData*>(error->data())->child;
            std::string message = stringOf(ctx->getProperty(child, "message"));
            if (!message.empty()) return message;
            return stringOf(ctx->getProperty(child, "name"));
        }
        default:
            return {};
    }
}

// 单测超时：到点后置 Context 停止标记，VM 在指令边界返回 Terminate。
class Watchdog {
public:
    Watchdog(IContext* ctx, int timeoutMs) {
        if (timeoutMs <= 0) return;
        thread_ = std::thread([this, ctx, timeoutMs] {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                              [this] { return done_; })) {
                timedOut_ = true;
                ctx->requestStop();
            }
        });
    }

    ~Watchdog() { stop(); }

    void stop() {
        if (!thread_.joinable()) return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }

    bool timedOut() const { return timedOut_; }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;
    bool timedOut_ = false;
    std::thread thread_;
};

enum class Phase { Ok, Parse, Sema, Compile, Runtime, Timeout };

struct ScriptOutcome {
    Phase phase = Phase::Ok;
    std::string name;
    std::string message;
    Value* value = nullptr;  // Ok 时为完成值；Runtime 时为错误值
};

struct HostState {
    IContext* ctx = nullptr;
    std::string test262Root;
    std::string filename;
    int timeoutMs = 0;
    bool verbose = false;
    std::string output;
};

thread_local HostState* g_host = nullptr;

ScriptOutcome executeScript(IContext* ctx, std::string source,
                            const std::string& filename, bool strict,
                            int timeoutMs);

Value* printNative(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    std::string text;
    for (size_t i = 0; i < argc; ++i) {
        if (i > 0) text += ' ';
        Value* str = neo::core::detail::toStringValue(ctx, argv[i]);
        if (neo::core::detail::isErrorValue(str)) return str;
        text += stringOf(str);
    }
    text += '\n';
    if (g_host != nullptr) {
        g_host->output += text;
        if (g_host->verbose) std::fwrite(text.data(), 1, text.size(), stdout);
    }
    return ctx->createUndefined();
}

Value* evalScriptNative(IContext* ctx, Value* /*self*/, size_t argc,
                        Value* argv[]) {
    if (argc == 0) return ctx->createUndefined();
    Value* src = neo::core::detail::toStringValue(ctx, argv[0]);
    if (neo::core::detail::isErrorValue(src)) return src;
    const std::string source = stringOf(src);
    const std::string filename =
        g_host != nullptr ? g_host->filename + " <evalScript>" : "<evalScript>";
    ScriptOutcome outcome = executeScript(ctx, source, filename, false, 0);
    switch (outcome.phase) {
        case Phase::Ok:
            return outcome.value != nullptr ? outcome.value
                                            : ctx->createUndefined();
        case Phase::Parse:
        case Phase::Sema:
        case Phase::Compile:
            return ctx->createErrorException(neo::core::ErrorKind::SyntaxError,
                                             outcome.message);
        case Phase::Runtime:
            return outcome.value;
        case Phase::Timeout:
            return ctx->createErrorException(neo::core::ErrorKind::Error,
                                             "evalScript timed out");
    }
    return ctx->createUndefined();
}

Value* gcNative(IContext* ctx, Value* /*self*/, size_t /*argc*/,
                Value* /*argv*/[]) {
    ctx->gc()->collect();
    return ctx->createUndefined();
}

Value* detachArrayBufferNative(IContext* ctx, Value* /*self*/, size_t argc,
                               Value* argv[]) {
    if (argc == 0) {
        return neo::core::detail::typeError(ctx, "detachArrayBuffer requires a buffer");
    }
    neo::core::ArrayBufferData* data = neo::core::asArrayBuffer(argv[0]);
    if (data == nullptr || data->isShared() || data->detached) {
        return neo::core::detail::typeError(ctx, "cannot detach this buffer");
    }
    data->bytes.clear();
    data->bytes.shrink_to_fit();
    data->detached = true;
    return ctx->createUndefined();
}

bool defineHostFunction(IContext* ctx, Value* target, const char* name,
                        neo::core::NativeFn fn, size_t minArgc) {
    Value* function = ctx->createNativeFunction(fn, minArgc);
    if (function == ctx->oomValue()) return false;
    ctx->setFunctionName(function, name);
    return ctx->defineProperty(target, name, function, true, false, true)
               ->type()
               ->kind() != TypeKind::Exception;
}

bool installHost(IContext* ctx) {
    Value* print = ctx->createNativeFunction(printNative, 0);
    if (print == ctx->oomValue()) return false;
    ctx->setFunctionName(print, "print");
    if (!ctx->defineGlobal("print", print, true, false, true)) return false;

    Value* host = ctx->createObject();
    if (host == ctx->oomValue()) return false;
    if (ctx->defineProperty(host, "global", ctx->globalObject(), false, false,
                            true)
            ->type()
            ->kind() == TypeKind::Exception) {
        return false;
    }
    if (!defineHostFunction(ctx, host, "evalScript", evalScriptNative, 1)) {
        return false;
    }
    if (!defineHostFunction(ctx, host, "gc", gcNative, 0)) return false;
    if (!defineHostFunction(ctx, host, "detachArrayBuffer",
                            detachArrayBufferNative, 1)) {
        return false;
    }
    return ctx->defineGlobal("$262", host, true, false, true);
}

ScriptOutcome executeScript(IContext* ctx, std::string source,
                            const std::string& filename, bool strict,
                            int timeoutMs) {
    ScriptOutcome outcome;
    if (strict) source = "\"use strict\";\n" + source;

    neo::frontend::Parser parser(
        source, neo::frontend::ParserOptions{neo::frontend::SourceType::Script});
    neo::frontend::ParseResult parsed = parser.parseProgram();
    if (!parsed.ok()) {
        outcome.phase = Phase::Parse;
        outcome.name = "SyntaxError";
        if (!parsed.errors.empty()) {
            const auto& error = parsed.errors.front();
            outcome.message = error.message + " (" +
                              std::to_string(error.location.line) + ":" +
                              std::to_string(error.location.column) + ")";
        }
        return outcome;
    }

    neo::sema::SemaResult sema = neo::sema::runSema(*parsed.program);
    if (!sema.ok()) {
        outcome.phase = Phase::Sema;
        outcome.name = "SyntaxError";
        const auto& error = sema.errors.front();
        outcome.message = error.message + " (" +
                          std::to_string(error.location.line) + ":" +
                          std::to_string(error.location.column) + ")";
        return outcome;
    }

    neo::compiler::Compiler compiler(
        static_cast<const neo::frontend::Program&>(*parsed.program), sema,
        neo::compiler::CompilerOptions{filename});
    neo::compiler::CompileResult compiled = compiler.compile();
    if (!compiled.ok()) {
        outcome.phase = Phase::Compile;
        outcome.name = "SyntaxError";
        if (!compiled.errors.empty()) {
            const auto& error = compiled.errors.front();
            outcome.message = error.message + " (" +
                              std::to_string(error.range.start.line) + ":" +
                              std::to_string(error.range.start.column) + ")";
        }
        return outcome;
    }

    neo::vm::Code* code = compiled.code.get();
    ctx->retainCode(std::move(compiled.code));

    Watchdog watchdog(ctx, timeoutMs);
    neo::vm::VM machine(ctx, code);
    Value* result = machine.run();
    watchdog.stop();

    if (watchdog.timedOut()) {
        outcome.phase = Phase::Timeout;
        outcome.name = "Timeout";
        outcome.message = "test exceeded " + std::to_string(timeoutMs) + "ms";
        return outcome;
    }
    if (result == nullptr) {
        outcome.value = ctx->createUndefined();
        return outcome;
    }
    if (neo::core::detail::isErrorValue(result) ||
        neo::core::detail::isInterruptValue(result)) {
        outcome.phase = Phase::Runtime;
        outcome.name = errorNameOf(ctx, result);
        outcome.message = errorMessageOf(ctx, result);
        outcome.value = result;
        return outcome;
    }
    outcome.value = result;
    return outcome;
}

bool shouldSkip(const Metadata& meta, const RunnerOptions& options,
                std::string& reason) {
    if (meta.hasFlag("module")) {
        reason = "module tests not implemented (M6-3)";
        return true;
    }
    if (meta.hasFlag("async")) {
        reason = "async tests not implemented (M6-3)";
        return true;
    }
    if (meta.hasFlag("CanBlockIsTrue")) {
        reason = "main context cannot block";
        return true;
    }
    if (meta.hasNegative && meta.negative.phase == "resolution") {
        reason = "module resolution not implemented (M6-3)";
        return true;
    }
    for (const std::string& feature : meta.features) {
        for (const std::string& skipped : options.skipFeatures) {
            if (feature == skipped) {
                reason = "feature not implemented: " + feature;
                return true;
            }
        }
    }
    for (const std::string& include : meta.includes) {
        if (include == "atomicsHelper.js" || include == "testAtomics.js") {
            reason = "$262.agent not implemented (M6-3)";
            return true;
        }
        if (include == "asyncHelpers.js") {
            reason = "async helpers not implemented (M6-3)";
            return true;
        }
        if (include == "temporalHelpers.js") {
            reason = "Temporal not implemented";
            return true;
        }
        if (include == "resizableArrayBufferUtils.js") {
            reason = "resizable ArrayBuffer not implemented";
            return true;
        }
    }
    return false;
}

bool runHarness(const RunnerOptions& options, IContext* ctx,
                const Metadata& meta, TestResult& result) {
    std::vector<std::string> files = {"sta.js", "assert.js"};
    for (const std::string& include : meta.includes) {
        if (include == "sta.js" || include == "assert.js") continue;
        files.push_back(include);
    }
    for (const std::string& file : files) {
        const std::filesystem::path path =
            std::filesystem::path(options.test262Root) / "harness" / file;
        const std::string source = readFile(path);
        if (source.empty()) {
            result.status = Status::Fail;
            result.reason = "missing harness file: " + file;
            return false;
        }
        ScriptOutcome outcome =
            executeScript(ctx, source, "harness/" + file, false, 0);
        if (outcome.phase != Phase::Ok) {
            result.status = Status::Fail;
            result.reason = "harness error in " + file + ": " + outcome.message;
            result.errorName = outcome.name;
            result.errorMessage = outcome.message;
            return false;
        }
    }
    return true;
}

bool runVariant(const RunnerOptions& options, const std::string& absPath,
                const std::string& variant, const Metadata& meta,
                TestResult& result) {
    auto alloc = neo::infra::createMallocAllocator();
    NullSink sink;
    neo::infra::Console console(&sink);
    std::unique_ptr<IContext> ctx = neo::core::createContext(alloc.get(), &console);

    // 尾调用用例需连续执行 10 万次调用；调试构建下耗时远超普通用例，
    // 对该 feature 单独放宽超时，避免把性能差异误判为失败。
    int timeoutMs = options.timeoutMs;
    if (timeoutMs > 0) {
        for (const std::string& feature : meta.features) {
            if (feature == "tail-call-optimization" && timeoutMs < 30000) {
                timeoutMs = 30000;
            }
        }
    }

    HostState host;
    host.ctx = ctx.get();
    host.test262Root = options.test262Root;
    host.filename = result.path;
    host.timeoutMs = timeoutMs;
    host.verbose = options.verbose;
    g_host = &host;

    if (!installHost(ctx.get())) {
        g_host = nullptr;
        result.status = Status::Fail;
        result.reason = "failed to install host functions";
        return false;
    }

    const bool raw = meta.hasFlag("raw");
    if (!raw && !runHarness(options, ctx.get(), meta, result)) {
        g_host = nullptr;
        return false;
    }

    const std::string source = readFile(absPath);
    const bool strict = variant == "strict";
    ScriptOutcome outcome =
        executeScript(ctx.get(), source, result.path, strict, timeoutMs);
    g_host = nullptr;

    if (meta.hasNegative) {
        const std::string& expectedPhase = meta.negative.phase;
        const std::string& expectedType = meta.negative.type;
        if (expectedPhase == "parse") {
            if (outcome.phase == Phase::Parse || outcome.phase == Phase::Sema ||
                outcome.phase == Phase::Compile) {
                if (expectedType.empty() || outcome.name == expectedType) {
                    return true;
                }
                result.status = Status::Fail;
                result.reason = "expected " + expectedType + " at parse, got " +
                                outcome.name;
                return false;
            }
            result.status = Status::Fail;
            result.reason = outcome.phase == Phase::Ok
                                ? "expected parse error, test completed"
                                : "expected parse error, got runtime error";
            result.errorName = outcome.name;
            result.errorMessage = outcome.message;
            return false;
        }
        if (expectedPhase == "runtime") {
            if (outcome.phase != Phase::Runtime) {
                result.status = Status::Fail;
                result.reason = outcome.phase == Phase::Ok
                                    ? "expected runtime error, test completed"
                                    : "expected runtime error, got " +
                                          outcome.name;
                return false;
            }
            if (!expectedType.empty() && outcome.name != expectedType) {
                result.status = Status::Fail;
                result.reason = "expected " + expectedType + ", got " +
                                outcome.name;
                result.errorName = outcome.name;
                result.errorMessage = outcome.message;
                return false;
            }
            return true;
        }
        result.status = Status::Fail;
        result.reason = "unsupported negative phase: " + expectedPhase;
        return false;
    }

    if (outcome.phase != Phase::Ok) {
        result.status = Status::Fail;
        if (outcome.phase == Phase::Timeout) {
            result.reason = outcome.message;
        } else if (outcome.phase == Phase::Runtime) {
            result.reason = "uncaught " + outcome.name;
        } else {
            result.reason = "compile error: " + outcome.message;
        }
        result.errorName = outcome.name;
        result.errorMessage = outcome.message;
        return false;
    }
    return true;
}

}  // namespace

const char* statusName(Status status) {
    switch (status) {
        case Status::Pass: return "PASS";
        case Status::Fail: return "FAIL";
        case Status::Skip: return "SKIP";
    }
    return "?";
}

TestResult runTest(const RunnerOptions& options, const std::string& absPath,
                   const std::string& relPath) {
    TestResult result;
    result.path = relPath;

    const std::string source = readFile(absPath);
    if (source.empty()) {
        result.status = Status::Fail;
        result.reason = "cannot read test file";
        return result;
    }

    const Metadata meta = parseMetadata(source);
    std::string skipReason;
    if (shouldSkip(meta, options, skipReason)) {
        result.status = Status::Skip;
        result.reason = std::move(skipReason);
        return result;
    }

    std::vector<std::string> variants;
    if (meta.hasFlag("raw")) {
        variants = {"raw"};
    } else if (meta.hasFlag("onlyStrict")) {
        variants = {"strict"};
    } else if (meta.hasFlag("noStrict")) {
        variants = {"sloppy"};
    } else {
        variants = {"sloppy", "strict"};
    }

    for (const std::string& variant : variants) {
        TestResult current;
        current.path = relPath;
        current.variant = variant;
        if (!runVariant(options, absPath, variant, meta, current)) {
            if (current.status == Status::Pass) current.status = Status::Fail;
            if (variants.size() > 1) {
                current.reason = "[" + variant + "] " + current.reason;
            }
            return current;
        }
    }
    result.status = Status::Pass;
    return result;
}

}  // namespace neo::test262
