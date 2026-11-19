// JsWorkerTest.cpp - JS Worker 门面端到端测试
//
// 主线程脚本 new Worker(path) → postMessage；worker 脚本 onmessage 回传；
// 主线程事件循环通过外部任务唤醒并派发 onmessage。
//
// 关联 ADR: 006（异常即值）、020（标准库组织）

#include "compiler/Compiler.h"
#include "core/IContext.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "frontend/Parser.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/IOutputSink.h"
#include "infra/MallocAllocator.h"
#include "sema/Sema.h"
#include "vm/VM.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

namespace {

using neo::core::IContext;
using neo::core::NumberData;
using neo::core::StringData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::infra::Console;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

class NullSink : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

struct Fixture {
    std::unique_ptr<neo::infra::IAllocator> alloc = createMallocAllocator();
    NullSink sink;
    Console console{&sink};
    std::unique_ptr<IContext> ctx = createContext(alloc.get(), &console);
};

// 解析 + Sema + 编译 + 执行一段 Script；返回 VM 结果。
Value* runScript(Fixture& f, const std::string& source,
                 const std::string& filename) {
    neo::frontend::Parser parser(
        source, neo::frontend::ParserOptions{neo::frontend::SourceType::Script});
    auto parsed = parser.parseProgram();
    EXPECT_TRUE(parsed.ok());
    if (!parsed.ok()) return nullptr;

    neo::sema::SemaResult sema = neo::sema::runSema(*parsed.program);
    EXPECT_TRUE(sema.ok());
    if (!sema.ok()) return nullptr;

    neo::compiler::Compiler compiler(
        static_cast<const neo::frontend::Program&>(*parsed.program), sema,
        neo::compiler::CompilerOptions{filename});
    auto compiled = compiler.compile();
    EXPECT_TRUE(compiled.ok());
    if (!compiled.ok()) return nullptr;

    neo::vm::Code* code = compiled.code.get();
    f.ctx->retainCode(std::move(compiled.code));
    neo::vm::VM machine(f.ctx.get(), code);
    return machine.run();
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << text;
}

}  // namespace

TEST(JsWorkerTest, PostMessageRoundTripThroughOnMessage) {
    Fixture f;

    const auto workerPath =
        std::filesystem::temp_directory_path() / "neojs_worker_echo.js";
    writeFile(workerPath,
              "onmessage = function (e) { postMessage({ got: e.data.x + 1 }); };\n");

    std::string mainSource =
        "var w = new Worker(\"" + workerPath.generic_string() + "\");\n"
        "w.onmessage = function (e) { globalThis.result = e.data.got; };\n"
        "w.postMessage({ x: 41 });\n";

    Value* result = runScript(f, mainSource, "main.js");
    ASSERT_NE(result, nullptr);
    ASSERT_FALSE(neo::core::detail::isErrorValue(result));

    // 泵主线程事件循环，等待 worker 回传（外部任务唤醒）。
    Value* value = nullptr;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        while (f.ctx->hasRunnableTask()) {
            (void)f.ctx->runOneTask();
        }
        value = f.ctx->getProperty(f.ctx->globalObject(), "result");
        if (value != nullptr && value->type()->kind() == TypeKind::Number) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->type()->kind(), TypeKind::Number);
    EXPECT_EQ(static_cast<NumberData*>(value->data())->value, 42.0);
}

TEST(JsWorkerTest, PostMessageRejectsUncloneableValue) {
    Fixture f;

    const auto workerPath =
        std::filesystem::temp_directory_path() / "neojs_worker_unused.js";
    writeFile(workerPath, "onmessage = function () {};\n");

    std::string mainSource =
        "var w = new Worker(\"" + workerPath.generic_string() + "\");\n"
        "var name = \"none\";\n"
        "try { w.postMessage(function () {}); }\n"
        "catch (e) { name = e.name; }\n"
        "globalThis.result = name;\n";

    Value* result = runScript(f, mainSource, "main.js");
    ASSERT_NE(result, nullptr);
    ASSERT_FALSE(neo::core::detail::isErrorValue(result));

    Value* value = f.ctx->getProperty(f.ctx->globalObject(), "result");
    ASSERT_EQ(value->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<StringData*>(value->data())->value, "DataCloneError");
}
