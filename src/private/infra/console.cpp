// console.cpp - StdoutSink 实现（FFI 收口：std::cout）
//
// 本文件是 neo.js 中唯一直接使用 std::cout 的地方（ADR-027 §5.6）。
// 业务代码通过 OutputSink*/Console 使用，测试注入 mock sink 截获输出。
//
// 关联 ADR: 027（FFI 封装）、028（infra）

#include "infra/console.h"

#include <iostream>

namespace neo::infra {

namespace {

// 进程级默认 sink（Console(nullptr) 时使用）
StdoutSink& default_sink() {
    static StdoutSink instance;
    return instance;
}

}  // namespace

// --- StdoutSink ---
void StdoutSink::write(std::string_view text) {
    // 标准库可能在写入时抛异常（如 bad_alloc），由调用方/上层 try-catch 兜底
    std::cout << text;
}

void StdoutSink::write_line(std::string_view text) {
    std::cout << text << '\n';
}

void StdoutSink::flush() {
    std::cout.flush();
}

// --- Console ---
Console::Console(OutputSink* sink)
    : sink_(sink != nullptr ? sink : &default_sink()) {}

void Console::write(std::string_view text) {
    sink_->write(text);
}

void Console::write_line(std::string_view text) {
    sink_->write_line(text);
}

}  // namespace neo::infra
