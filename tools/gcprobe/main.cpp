// gcprobe/main.cpp - P0 GC 重构评估探针
//
// 只读统计：不改变任何语义，仅读取 GC/Context 的常开计数器。
// 对每个代表性 workload 记录增量：
//   - Value 分配总数、按 TypeKind 分布
//   - createTracked / createEphemeral / direct 路径占比
//   - data 分配数
//   - collect 次数、collect 耗时占比
//   - values_ 峰值
//   - collect 时作用域树快照（values_ 中命名绑定的近似占比）
//
// 用途：判断 GC 压力主要来自 primitive 还是对象，从而决定是否投入
// C++ 所有权倒置（P3）或优先做立即数化。

#include "compiler/Compiler.h"
#include "core/Context.h"
#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "frontend/Parser.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "sema/Sema.h"
#include "vm/VM.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using neo::compiler::Compiler;
using neo::compiler::CompilerOptions;
using neo::core::Context;
using neo::core::GCStats;
using neo::core::IContext;
using neo::core::IGCEngine;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::ValueCreationStats;
using neo::frontend::Parser;
using neo::frontend::ParserOptions;
using neo::frontend::Program;
using neo::frontend::SourceType;
using neo::infra::Console;
using neo::infra::createMallocAllocator;
using neo::infra::IAllocator;
using neo::infra::IOutputSink;
using neo::sema::runSema;
using neo::vm::VM;

class NullSink final : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

struct Snapshot {
    GCStats gc;
    ValueCreationStats creation;
    size_t used = 0;
};

struct Delta {
    uint64_t values = 0;
    uint64_t tracked = 0;
    uint64_t ephemeral = 0;
    uint64_t direct = 0;
    uint64_t data = 0;
    uint64_t collections = 0;
    uint64_t collected = 0;
    uint64_t collectNanos = 0;
    uint64_t wallNanos = 0;
    uint64_t maxValueCount = 0;
    uint64_t maxScopeValues = 0;
    uint64_t maxScopeCount = 0;
    uint64_t lastScopeValues = 0;
    uint64_t lastNamedBindings = 0;
    uint64_t byKind[neo::core::kTypeKindCount]{};
    size_t usedBefore = 0;
    size_t usedAfter = 0;
};

const char* typeKindName(TypeKind kind) {
    switch (kind) {
        case TypeKind::Undefined: return "Undefined";
        case TypeKind::Null: return "Null";
        case TypeKind::Boolean: return "Boolean";
        case TypeKind::Number: return "Number";
        case TypeKind::BigInt: return "BigInt";
        case TypeKind::String: return "String";
        case TypeKind::Exception: return "Exception";
        case TypeKind::Fatal: return "Fatal";
        case TypeKind::Uninitialized: return "Uninitialized";
        case TypeKind::Function: return "Function";
        case TypeKind::Object: return "Object";
        case TypeKind::Array: return "Array";
        case TypeKind::Symbol: return "Symbol";
        case TypeKind::PrimitiveWrapper: return "PrimitiveWrapper";
        case TypeKind::ArrayIterator: return "ArrayIterator";
        case TypeKind::StringIterator: return "StringIterator";
        case TypeKind::IteratorWrapper: return "IteratorWrapper";
        case TypeKind::IteratorHelper: return "IteratorHelper";
        case TypeKind::Collection: return "Collection";
        case TypeKind::CollectionIterator: return "CollectionIterator";
        case TypeKind::Date: return "Date";
        case TypeKind::ArrayBuffer: return "ArrayBuffer";
        case TypeKind::DataView: return "DataView";
        case TypeKind::TypedArray: return "TypedArray";
        case TypeKind::Error: return "Error";
        case TypeKind::Promise: return "Promise";
        case TypeKind::Interrupt: return "Interrupt";
        case TypeKind::Coroutine: return "Coroutine";
        case TypeKind::AsyncGenerator: return "AsyncGenerator";
        case TypeKind::ModuleNamespace: return "ModuleNamespace";
        case TypeKind::Proxy: return "Proxy";
        case TypeKind::RegExp: return "RegExp";
        case TypeKind::RegExpStringIterator: return "RegExpStringIterator";
        case TypeKind::Arguments: return "Arguments";
        case TypeKind::IntlNumberFormat: return "IntlNumberFormat";
        case TypeKind::IntlDateTimeFormat: return "IntlDateTimeFormat";
        case TypeKind::IntlCollator: return "IntlCollator";
        case TypeKind::IntlPluralRules: return "IntlPluralRules";
        case TypeKind::IntlRelativeTimeFormat: return "IntlRelativeTimeFormat";
        case TypeKind::IntlListFormat: return "IntlListFormat";
        case TypeKind::IntlDisplayNames: return "IntlDisplayNames";
        case TypeKind::IntlSegmenter: return "IntlSegmenter";
        case TypeKind::IntlSegments: return "IntlSegments";
        case TypeKind::IntlLocale: return "IntlLocale";
        case TypeKind::Worker: return "Worker";
        case TypeKind::MessagePort: return "MessagePort";
        case TypeKind::SharedArrayBuffer: return "SharedArrayBuffer";
        case TypeKind::WeakRef: return "WeakRef";
    }
    return "Unknown";
}

Snapshot take(IContext* ctx) {
    Snapshot s;
    s.gc = ctx->gc()->stats();
    s.creation = static_cast<Context*>(ctx)->creationStats();
    s.used = ctx->allocator()->used();
    return s;
}

Delta diff(const Snapshot& before, const Snapshot& after, uint64_t wallNanos) {
    Delta d;
    d.values = after.gc.valueAllocations - before.gc.valueAllocations;
    d.data = after.gc.dataAllocations - before.gc.dataAllocations;
    d.collections = after.gc.collections - before.gc.collections;
    d.collected = after.gc.collectedValues - before.gc.collectedValues;
    d.collectNanos = after.gc.collectNanos - before.gc.collectNanos;
    d.wallNanos = wallNanos;
    d.maxValueCount = after.gc.maxValueCount;
    d.maxScopeValues = after.gc.maxScopeValues;
    d.maxScopeCount = after.gc.maxScopeCount;
    d.lastScopeValues = after.gc.lastScopeValues;
    d.lastNamedBindings = after.gc.lastNamedBindings;
    d.tracked = after.creation.tracked - before.creation.tracked;
    d.ephemeral = after.creation.ephemeral - before.creation.ephemeral;
    d.direct = d.values >= d.tracked + d.ephemeral
                   ? d.values - d.tracked - d.ephemeral
                   : 0;
    for (size_t i = 0; i < neo::core::kTypeKindCount; ++i) {
        d.byKind[i] =
            after.creation.byKind[i] - before.creation.byKind[i];
    }
    d.usedBefore = before.used;
    d.usedAfter = after.used;
    return d;
}

bool runScript(IContext* ctx, const std::string& source,
               const std::string& name) {
    Parser parser(source, ParserOptions{SourceType::Script});
    auto parsed = parser.parseProgram();
    if (!parsed.ok()) {
        std::fprintf(stderr, "[%s] parse error\n", name.c_str());
        return false;
    }
    auto sema = runSema(*parsed.program);
    if (!sema.ok()) {
        std::fprintf(stderr, "[%s] sema error: %s\n", name.c_str(),
                     sema.errors.empty() ? "?" : sema.errors.front().message.c_str());
        return false;
    }
    Compiler compiler(static_cast<const Program&>(*parsed.program), sema,
                      CompilerOptions{name});
    auto compiled = compiler.compile();
    if (!compiled.ok()) {
        std::fprintf(stderr, "[%s] compile error\n", name.c_str());
        return false;
    }
    neo::vm::Code* code = compiled.code.get();
    ctx->retainCode(std::move(compiled.code));

    VM machine(ctx, code);
    Value* result = machine.run();
    if (neo::core::detail::isErrorValue(result)) {
        Value* errorName = ctx->getProperty(result, "name");
        Value* message = ctx->getProperty(result, "message");
        std::fprintf(stderr, "[%s] runtime error: %s: %s\n", name.c_str(),
                     errorName != nullptr &&
                             errorName->type()->kind() == TypeKind::String
                         ? static_cast<neo::core::StringData*>(errorName->data())
                               ->value.c_str()
                         : "?",
                     message != nullptr &&
                             message->type()->kind() == TypeKind::String
                         ? static_cast<neo::core::StringData*>(message->data())
                               ->value.c_str()
                         : "?");
        return false;
    }
    return true;
}

void printDelta(const std::string& name, const Delta& d) {
    const double wallMs = static_cast<double>(d.wallNanos) / 1e6;
    const double collectMs = static_cast<double>(d.collectNanos) / 1e6;
    const double gcShare =
        d.wallNanos == 0 ? 0.0 : 100.0 * static_cast<double>(d.collectNanos) /
                                      static_cast<double>(d.wallNanos);
    const double trackedPct =
        d.values == 0 ? 0.0 : 100.0 * static_cast<double>(d.tracked) /
                                    static_cast<double>(d.values);
    const double ephemeralPct =
        d.values == 0 ? 0.0 : 100.0 * static_cast<double>(d.ephemeral) /
                                    static_cast<double>(d.values);
    const double directPct =
        d.values == 0 ? 0.0 : 100.0 * static_cast<double>(d.direct) /
                                    static_cast<double>(d.values);

    std::printf("\n== %s ==\n", name.c_str());
    std::printf("  wall %.2f ms | collect %.2f ms (%.1f%%) | collections %llu | "
                "collected %llu\n",
                wallMs, collectMs, gcShare,
                static_cast<unsigned long long>(d.collections),
                static_cast<unsigned long long>(d.collected));
    std::printf("  values %llu | tracked %llu (%.1f%%) | ephemeral %llu (%.1f%%) "
                "| direct %llu (%.1f%%) | data %llu\n",
                static_cast<unsigned long long>(d.values),
                static_cast<unsigned long long>(d.tracked), trackedPct,
                static_cast<unsigned long long>(d.ephemeral), ephemeralPct,
                static_cast<unsigned long long>(d.direct), directPct,
                static_cast<unsigned long long>(d.data));
    std::printf("  peak values_ %llu | peak scope values %llu | peak scopes %llu\n",
                static_cast<unsigned long long>(d.maxValueCount),
                static_cast<unsigned long long>(d.maxScopeValues),
                static_cast<unsigned long long>(d.maxScopeCount));
    std::printf("  last collect: scope values %llu | named bindings %llu | "
                "anonymous approx %.1f%%\n",
                static_cast<unsigned long long>(d.lastScopeValues),
                static_cast<unsigned long long>(d.lastNamedBindings),
                d.lastScopeValues == 0
                    ? 0.0
                    : 100.0 *
                          (1.0 - static_cast<double>(d.lastNamedBindings) /
                                     static_cast<double>(d.lastScopeValues)));
    std::printf("  allocator used %zu -> %zu bytes\n", d.usedBefore, d.usedAfter);

    // 按分配量降序打印非零类型。
    std::vector<std::pair<uint64_t, TypeKind>> kinds;
    for (size_t i = 0; i < neo::core::kTypeKindCount; ++i) {
        if (d.byKind[i] != 0) {
            kinds.emplace_back(d.byKind[i], static_cast<TypeKind>(i));
        }
    }
    std::sort(kinds.begin(), kinds.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::printf("  by kind:\n");
    for (const auto& [count, kind] : kinds) {
        const double pct =
            d.values == 0 ? 0.0
                          : 100.0 * static_cast<double>(count) /
                                static_cast<double>(d.values);
        std::printf("    %-22s %10llu  %5.1f%%\n", typeKindName(kind),
                    static_cast<unsigned long long>(count), pct);
    }
}

struct Workload {
    const char* name;
    const char* source;
};

}  // namespace

int main() {
    const std::vector<Workload> workloads = {
        {"arith-let",
         "let s = 0;\n"
         "for (let i = 0; i < 200000; i++) { s = s + i * 2 - 1; }\n"},
        // 对照：var 循环没有逐迭代绑定，循环体临时值全部落到函数作用域，
        // 在 values_ 仍是根的前提下无法提前回收。用于量化「移除局部优化」后的
        // 内存/GC 压力，验证 C++ 所有权倒置的必要性。
        {"arith-var",
         "function f() {\n"
         "  var s = 0;\n"
         "  for (var i = 0; i < 200000; i++) { s = s + i * 2 - 1; }\n"
         "  return s;\n"
         "}\n"
         "f();\n"},
        // 对照：while 循环体没有逐迭代作用域（编译器只为 for 建 iterationEnv），
        // 临时值全部锚定在函数作用域，直到函数返回。这是「移除局部优化」后
        // 的真实形态，用于量化 values_ 作为根导致的线性内存增长。
        {"arith-while",
         "function f() {\n"
         "  var s = 0;\n"
         "  var i = 0;\n"
         "  while (i < 200000) { s = s + i * 2 - 1; i = i + 1; }\n"
         "  return s;\n"
         "}\n"
         "f();\n"},
        // 对照：循环体不是 BlockStatement（无花括号），编译器不建块作用域，
        // 临时值全部锚定在函数作用域直到返回。这是 values_ 作为根的真实代价。
        {"arith-while-noblock",
         "function f() {\n"
         "  var s = 0;\n"
         "  var i = 0;\n"
         "  while (i < 200000) s = s + i * 2 - 1, i = i + 1;\n"
         "  return s;\n"
         "}\n"
         "f();\n"},
        {"strings",
         "let s = '';\n"
         "for (let i = 0; i < 20000; i++) {\n"
         "  s = s + 'x';\n"
         "  if (s.length > 500) { s = ''; }\n"
         "}\n"},
        {"objects",
         "let a = [];\n"
         "for (let i = 0; i < 20000; i++) {\n"
         "  a.push({ x: i, y: i + 1 });\n"
         "  if (a.length > 100) { a.shift(); }\n"
         "}\n"},
        {"calls",
         "function f(x) { return x + 1; }\n"
         "let s = 0;\n"
         "for (let i = 0; i < 100000; i++) { s = f(s); }\n"},
        {"closures",
         "let gs = [];\n"
         "for (let i = 0; i < 10000; i++) {\n"
         "  gs.push(function () { return i; });\n"
         "}\n"
         "let s = 0;\n"
         "for (let i = 0; i < gs.length; i++) { s = s + gs[i](); }\n"},
        {"props",
         "let o = { a: 0 };\n"
         "for (let i = 0; i < 200000; i++) { o.a = o.a + 1; }\n"},
        {"regex",
         "let r = /a+/;\n"
         "let s = 0;\n"
         "for (let i = 0; i < 20000; i++) {\n"
         "  if (r.test('aaa')) { s = s + 1; }\n"
         "}\n"},
        {"json",
         "let s = 0;\n"
         "for (let i = 0; i < 5000; i++) {\n"
         "  let o = { a: i, b: [1, 2, 3] };\n"
         "  s = s + JSON.stringify(o).length;\n"
         "}\n"},
    };

    std::printf("neo.js GC probe (P0)\n");
    std::printf("(每个 workload 使用独立 Context，bootstrap 不计入增量)\n");

    bool printedBootstrap = false;
    for (const Workload& w : workloads) {
        auto alloc = createMallocAllocator();
        NullSink sink;
        Console console(&sink);
        std::unique_ptr<IContext> ctx =
            neo::core::createContext(alloc.get(), &console);

        if (!printedBootstrap) {
            std::printf("bootstrap: values=%llu data=%llu used=%zu bytes\n",
                        static_cast<unsigned long long>(
                            ctx->gc()->stats().valueAllocations),
                        static_cast<unsigned long long>(
                            ctx->gc()->stats().dataAllocations),
                        ctx->allocator()->used());
            printedBootstrap = true;
        }

        // 包进 IIFE：避免顶层 let/const 绑定在脚本结束后仍被 global 作用域锚定，
        // 使 final live 能反映真实泄漏；同时更接近真实代码形态。
        const std::string wrapped =
            "(function () {\n" + std::string(w.source) + "\n})();\n";

        const Snapshot before = take(ctx.get());
        const auto start = std::chrono::steady_clock::now();
        const bool ok = runScript(ctx.get(), wrapped, w.name);
        const auto end = std::chrono::steady_clock::now();
        const uint64_t wallNanos = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
                .count());
        if (!ok) {
            std::fprintf(stderr, "workload '%s' failed\n", w.name);
            continue;
        }
        const Snapshot after = take(ctx.get());
        printDelta(w.name, diff(before, after, wallNanos));

        // 显式 collect 后确认探针本身不制造泄漏。
        ctx->gc()->collect();
        std::printf("  final live values=%zu collected=%zu used=%zu bytes\n",
                    ctx->gc()->valueCount(), ctx->gc()->collectedCount(),
                    ctx->allocator()->used());
    }
    return 0;
}
