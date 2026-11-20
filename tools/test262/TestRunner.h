// TestRunner.h - 单个 Test262 用例执行器
//
// 每个用例（每个 strict 变体）创建独立 Context，先执行 harness
// （sta.js + assert.js + includes），再执行测试源。支持：
//   - flags: onlyStrict / noStrict / raw（module/async 在 M6-1 跳过）
//   - negative: phase/type
//   - $262.global / evalScript / gc / detachArrayBuffer
//   - VM 停止标记实现的单测超时
//
// 关联 ADR: 006（异常即值）、027（测试规范）

#ifndef NEO_TOOLS_TEST262_TESTRUNNER_H
#define NEO_TOOLS_TEST262_TESTRUNNER_H

#include <string>
#include <vector>

namespace neo::test262 {

struct RunnerOptions {
    std::string test262Root;
    std::vector<std::string> skipFeatures;
    int timeoutMs = 5000;
    bool verbose = false;
};

enum class Status { Pass, Fail, Skip };

struct TestResult {
    std::string path;
    Status status = Status::Pass;
    std::string variant;       // "sloppy" / "strict" / "raw"
    std::string reason;        // Skip/Fail 的简短原因
    std::string errorName;
    std::string errorMessage;
};

// 执行一个用例；relPath 用于展示，absPath 用于读取。
// 返回该用例（含两个 strict 变体）的汇总结果。
TestResult runTest(const RunnerOptions& options, const std::string& absPath,
                   const std::string& relPath);

const char* statusName(Status status);

}  // namespace neo::test262

#endif  // NEO_TOOLS_TEST262_TESTRUNNER_H
