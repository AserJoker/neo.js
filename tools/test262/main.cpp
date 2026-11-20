// main.cpp - Test262 跑批入口
//
// 用法：
//   neo_test262 --test262-root <dir> [--suite <rel>] [--filter <substr>]
//               [--limit N] [--skip-features a,b] [--timeout-ms N]
//               [--json <path>] [--verbose] [--include-intl]
//               [--no-isolation] [--chunk-size N]
//
// 默认按 chunk 启动子进程执行：单个用例触发引擎崩溃时只丢失该用例，
// 父进程会用单用例子进程重跑该 chunk 定位崩溃点并继续。--no-isolation
// 用于调试 runner 自身。
//
// 默认跳过 staging/ 与 intl402/（M6-1 聚焦语言与内建核心）。
//
// 关联 ADR: 027（测试规范）

#include "TestRunner.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace {

struct Options {
    std::string test262Root;
    std::string suite;
    std::string filter;
    std::string jsonPath;
    std::vector<std::string> skipFeatures;
    size_t limit = 0;
    size_t chunkSize = 25;
    int timeoutMs = 5000;
    bool verbose = false;
    bool includeIntl = false;
    bool noIsolation = false;
    // 子进程模式：从 listFile 读取用例，把 TSV 结果写入 resultFile。
    std::string workerList;
    std::string workerResult;
};

void usage() {
    std::cout
        << "Usage: neo_test262 --test262-root <dir> [options]\n"
        << "  --suite <rel>          只跑 test/<rel> 子目录\n"
        << "  --filter <substr>      只跑路径包含子串的用例\n"
        << "  --limit <n>            最多跑 n 个用例\n"
        << "  --skip-features <a,b>  追加跳过的 feature（逗号分隔）\n"
        << "  --timeout-ms <n>       单用例超时（默认 5000，0 关闭）\n"
        << "  --json <path>          输出 JSON 结果\n"
        << "  --verbose              逐用例打印\n"
        << "  --include-intl         包含 intl402/\n"
        << "  --no-isolation         在父进程内直接执行（调试 runner 用）\n"
        << "  --chunk-size <n>       子进程每批用例数（默认 25）\n";
}

bool parseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::string& out) {
            if (i + 1 >= argc) return false;
            out = argv[++i];
            return true;
        };
        if (arg == "--help" || arg == "-h") {
            usage();
            std::exit(0);
        } else if (arg == "--test262-root") {
            if (!next(options.test262Root)) return false;
        } else if (arg == "--suite") {
            if (!next(options.suite)) return false;
        } else if (arg == "--filter") {
            if (!next(options.filter)) return false;
        } else if (arg == "--limit") {
            std::string value;
            if (!next(value)) return false;
            options.limit = static_cast<size_t>(std::stoul(value));
        } else if (arg == "--skip-features") {
            std::string value;
            if (!next(value)) return false;
            std::stringstream stream(value);
            std::string item;
            while (std::getline(stream, item, ',')) {
                if (!item.empty()) options.skipFeatures.push_back(item);
            }
        } else if (arg == "--timeout-ms") {
            std::string value;
            if (!next(value)) return false;
            options.timeoutMs = std::stoi(value);
        } else if (arg == "--json") {
            if (!next(options.jsonPath)) return false;
        } else if (arg == "--verbose") {
            options.verbose = true;
        } else if (arg == "--include-intl") {
            options.includeIntl = true;
        } else if (arg == "--no-isolation") {
            options.noIsolation = true;
        } else if (arg == "--chunk-size") {
            std::string value;
            if (!next(value)) return false;
            options.chunkSize = static_cast<size_t>(std::stoul(value));
            if (options.chunkSize == 0) options.chunkSize = 1;
        } else if (arg == "--worker") {
            if (!next(options.workerList)) return false;
            if (!next(options.workerResult)) return false;
        } else {
            std::cerr << "unknown option: " << arg << "\n";
            return false;
        }
    }
    return true;
}

std::string envRoot() {
#ifdef _WIN32
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, "TEST262_ROOT") == 0 && value != nullptr) {
        std::string out(value);
        std::free(value);
        return out;
    }
    return {};
#else
    const char* value = std::getenv("TEST262_ROOT");
    return value != nullptr ? value : "";
#endif
}

std::string defaultRoot() {
    const std::string env = envRoot();
    if (!env.empty()) return env;
    const char* candidates[] = {"third_party/test262", "build/test262",
                                "../test262", "test262"};
    for (const char* candidate : candidates) {
        if (fs::is_directory(candidate)) return candidate;
    }
    return {};
}

// M6-1 默认跳过明确未实现的 feature；可用 --skip-features 追加。
std::vector<std::string> defaultSkipFeatures() {
    return {
        "Temporal",
        "decorators",
        "decorator-metadata",
        "ShadowRealm",
        "explicit-resource-management",
        "import-attributes",
        "import-defer",
        "source-phase-imports",
        "resizable-arraybuffer",
        "growable-sharedarraybuffer",
        "arraybuffer-transfer",
        "Float16Array",
        "json-parse-with-source",
        "change-array-by-copy",
        "array-from-async",
        "set-methods",
        "upsert",
        "promise-try",
        "regexp-duplicate-named-groups",
        "well-formed-unicode-strings",
        "cross-realm",
        "IsHTMLDDA",
        "host-gc-required",
        "non-deterministic",
    };
}

bool isSkippedDir(const fs::path& path, bool includeIntl) {
    const std::string name = path.filename().string();
    if (name == "staging") return true;
    if (!includeIntl && name == "intl402") return true;
    return false;
}

std::string jsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char ch : text) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    out += "\\u00";
                    const char* hex = "0123456789abcdef";
                    out += hex[(static_cast<unsigned char>(ch) >> 4) & 0x0F];
                    out += hex[static_cast<unsigned char>(ch) & 0x0F];
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

std::string suiteOf(const std::string& path) {
    const size_t first = path.find('/');
    if (first == std::string::npos) return path;
    const size_t second = path.find('/', first + 1);
    if (second == std::string::npos) return path.substr(0, first);
    return path.substr(0, second);
}

std::string sanitizeField(std::string text) {
    for (char& ch : text) {
        if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' ';
    }
    return text;
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    for (size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == '\t') {
            fields.push_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
    return fields;
}

bool statusFromName(const std::string& name, neo::test262::Status& status) {
    if (name == "PASS") status = neo::test262::Status::Pass;
    else if (name == "FAIL") status = neo::test262::Status::Fail;
    else if (name == "SKIP") status = neo::test262::Status::Skip;
    else return false;
    return true;
}

void writeResultLine(std::ofstream& out, const neo::test262::TestResult& result) {
    out << neo::test262::statusName(result.status) << '\t'
        << sanitizeField(result.variant) << '\t' << sanitizeField(result.path)
        << '\t' << sanitizeField(result.reason) << '\t'
        << sanitizeField(result.errorName) << '\t'
        << sanitizeField(result.errorMessage) << '\n';
    out.flush();
}

#ifndef _WIN32
std::string shellQuote(const std::string& text) {
    std::string out = "\"";
    for (const char ch : text) {
        if (ch == '"') out += "\\\"";
        else out += ch;
    }
    out += '"';
    return out;
}
#endif

int spawnChild(const std::string& executable,
               const std::vector<std::string>& args) {
#ifdef _WIN32
    // 不经 shell：避免 cmd 对引号/路径的二次解析。
    std::vector<const char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(executable.c_str());
    for (const std::string& arg : args) argv.push_back(arg.c_str());
    argv.push_back(nullptr);
    const intptr_t code = _spawnv(_P_WAIT, executable.c_str(), argv.data());
    return static_cast<int>(code);
#else
    std::string command = shellQuote(executable);
    for (const std::string& arg : args) command += " " + shellQuote(arg);
    command += " > /dev/null 2>&1";
    int code = std::system(command.c_str());
    if (code != -1) code = WEXITSTATUS(code);
    return code;
#endif
}

std::string tempPath(const char* suffix, size_t counter) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return (fs::temp_directory_path() /
            ("neo_test262_" + std::to_string(stamp) + "_" +
             std::to_string(counter) + suffix))
        .string();
}

// 子进程模式：读取用例列表，逐条执行并把 TSV 结果写入 resultFile。
int runWorkerMode(const Options& options) {
    std::ifstream list(options.workerList, std::ios::binary);
    if (!list.is_open()) {
        std::cerr << "cannot read worker list: " << options.workerList << "\n";
        return 2;
    }
    std::ofstream out(options.workerResult,
                      std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "cannot write worker result: " << options.workerResult << "\n";
        return 2;
    }

    neo::test262::RunnerOptions runnerOptions;
    runnerOptions.test262Root = options.test262Root;
    runnerOptions.skipFeatures = defaultSkipFeatures();
    runnerOptions.skipFeatures.insert(runnerOptions.skipFeatures.end(),
                                      options.skipFeatures.begin(),
                                      options.skipFeatures.end());
    runnerOptions.timeoutMs = options.timeoutMs;

    std::string line;
    while (std::getline(list, line)) {
        if (line.empty()) continue;
        // 列表行：<相对路径>\t<绝对/调用方路径>。
        std::string rel;
        std::string path = line;
        const size_t tab = line.find('\t');
        if (tab != std::string::npos) {
            rel = line.substr(0, tab);
            path = line.substr(tab + 1);
        } else {
            rel = fs::path(line).filename().generic_string();
        }
        neo::test262::TestResult result =
            neo::test262::runTest(runnerOptions, path, rel);
        writeResultLine(out, result);
    }
    out << "DONE\n";
    out.flush();
    return 0;
}

// 读取子进程结果；返回是否完整（DONE 且条数匹配）。
bool readChildResults(const std::string& path, size_t expected,
                      std::vector<neo::test262::TestResult>& results) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    bool done = false;
    std::string line;
    while (std::getline(in, line)) {
        if (line == "DONE") {
            done = true;
            continue;
        }
        if (line.empty()) continue;
        const std::vector<std::string> fields = splitTabs(line);
        if (fields.size() != 6) continue;
        neo::test262::TestResult result;
        if (!statusFromName(fields[0], result.status)) continue;
        result.variant = fields[1];
        result.path = fields[2];
        result.reason = fields[3];
        result.errorName = fields[4];
        result.errorMessage = fields[5];
        results.push_back(std::move(result));
    }
    return done && results.size() == expected;
}

std::vector<std::string> childArgs(const Options& options,
                                   const std::string& listPath,
                                   const std::string& resultPath) {
    std::vector<std::string> args = {
        "--worker", listPath, resultPath,
        "--test262-root", options.test262Root,
        "--timeout-ms", std::to_string(options.timeoutMs),
    };
    if (!options.skipFeatures.empty()) {
        std::string joined;
        for (const std::string& feature : options.skipFeatures) {
            if (!joined.empty()) joined += ',';
            joined += feature;
        }
        args.push_back("--skip-features");
        args.push_back(joined);
    }
    return args;
}

// 执行一个 chunk；若子进程崩溃，则逐条重跑以定位崩溃用例。
void runChunk(const std::string& executable, const Options& options,
              const fs::path& base, const std::vector<std::string>& files,
              std::vector<neo::test262::TestResult>& results,
              size_t& counter) {
    const std::string listPath = tempPath(".list", counter);
    const std::string resultPath = tempPath(".tsv", counter);
    ++counter;
    {
        std::ofstream list(listPath, std::ios::binary | std::ios::trunc);
        for (const std::string& file : files) {
            list << fs::relative(fs::path(file), base).generic_string() << '\t'
                 << file << '\n';
        }
    }

    const int code = spawnChild(executable, childArgs(options, listPath, resultPath));
    std::vector<neo::test262::TestResult> chunk;
    const bool complete = readChildResults(resultPath, files.size(), chunk);
    fs::remove(listPath);
    fs::remove(resultPath);

    if (complete) {
        for (auto& result : chunk) results.push_back(std::move(result));
        return;
    }

    // chunk 内至少一个用例崩溃：逐条重跑，崩溃用例记为 FAIL。
    for (const std::string& file : files) {
        const std::string oneList = tempPath(".list", counter);
        const std::string oneResult = tempPath(".tsv", counter);
        ++counter;
        {
            std::ofstream list(oneList, std::ios::binary | std::ios::trunc);
            list << fs::relative(fs::path(file), base).generic_string() << '\t'
                 << file << '\n';
        }
        const int oneCode =
            spawnChild(executable, childArgs(options, oneList, oneResult));
        std::vector<neo::test262::TestResult> single;
        const bool ok = readChildResults(oneResult, 1, single);
        fs::remove(oneList);
        fs::remove(oneResult);
        if (ok) {
            results.push_back(std::move(single.front()));
        } else {
            neo::test262::TestResult crashed;
            crashed.path = fs::relative(fs::path(file), base).generic_string();
            crashed.status = neo::test262::Status::Fail;
            crashed.reason = "crashed (child exit " + std::to_string(oneCode) +
                             ", parent chunk exit " + std::to_string(code) + ")";
            crashed.errorName = "Crash";
            results.push_back(std::move(crashed));
        }
    }
}

void runInProcess(const Options& options,
                  const std::vector<std::string>& files,
                  const fs::path& testBase,
                  std::vector<neo::test262::TestResult>& results) {
    neo::test262::RunnerOptions runnerOptions;
    runnerOptions.test262Root = options.test262Root;
    runnerOptions.skipFeatures = defaultSkipFeatures();
    runnerOptions.skipFeatures.insert(runnerOptions.skipFeatures.end(),
                                      options.skipFeatures.begin(),
                                      options.skipFeatures.end());
    runnerOptions.timeoutMs = options.timeoutMs;
    runnerOptions.verbose = options.verbose;
    for (const std::string& file : files) {
        const std::string rel =
            fs::relative(fs::path(file), testBase).generic_string();
        results.push_back(neo::test262::runTest(runnerOptions, file, rel));
    }
}

void reportResults(const Options& options,
                   const std::vector<neo::test262::TestResult>& results) {
    size_t pass = 0;
    size_t fail = 0;
    size_t skip = 0;
    std::map<std::string, size_t> failBySuite;
    for (const auto& result : results) {
        switch (result.status) {
            case neo::test262::Status::Pass: ++pass; break;
            case neo::test262::Status::Fail:
                ++fail;
                ++failBySuite[suiteOf(result.path)];
                break;
            case neo::test262::Status::Skip: ++skip; break;
        }
        if (options.verbose || result.status == neo::test262::Status::Fail) {
            std::cout << neo::test262::statusName(result.status) << " "
                      << result.path;
            if (!result.variant.empty()) std::cout << " [" << result.variant << "]";
            if (!result.reason.empty()) std::cout << " - " << result.reason;
            std::cout << "\n" << std::flush;
        }
    }

    std::cout << "\n== Test262 summary ==\n";
    std::cout << "total: " << results.size() << "  pass: " << pass
              << "  fail: " << fail << "  skip: " << skip << "\n";
    if (!failBySuite.empty()) {
        std::cout << "failures by suite:\n";
        for (const auto& [suite, count] : failBySuite) {
            std::cout << "  " << suite << ": " << count << "\n";
        }
    }

    if (!options.jsonPath.empty()) {
        std::ofstream out(options.jsonPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            std::cerr << "cannot write json: " << options.jsonPath << "\n";
            return;
        }
        out << "[\n";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& result = results[i];
            out << "  {\"path\":\"" << jsonEscape(result.path)
                << "\",\"status\":\"" << neo::test262::statusName(result.status)
                << "\",\"variant\":\"" << jsonEscape(result.variant)
                << "\",\"reason\":\"" << jsonEscape(result.reason)
                << "\",\"errorName\":\"" << jsonEscape(result.errorName)
                << "\",\"errorMessage\":\"" << jsonEscape(result.errorMessage)
                << "\"}";
            if (i + 1 != results.size()) out << ",";
            out << "\n";
        }
        out << "]\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) {
        usage();
        return 2;
    }
    if (options.test262Root.empty()) options.test262Root = defaultRoot();
    if (options.test262Root.empty()) {
        std::cerr << "missing --test262-root (or TEST262_ROOT)\n";
        return 2;
    }
    if (!options.workerList.empty()) return runWorkerMode(options);

    const fs::path root(options.test262Root);
    fs::path testBase = root;
    if (fs::is_directory(root / "test")) testBase = root / "test";
    fs::path base = testBase;
    if (!options.suite.empty()) base /= options.suite;
    if (!fs::is_directory(base)) {
        std::cerr << "test directory not found: " << base << "\n";
        return 2;
    }

    std::vector<std::string> files;
    for (const fs::directory_entry& entry :
         fs::recursive_directory_iterator(base)) {
        if (!entry.is_regular_file()) continue;
        const fs::path& path = entry.path();
        if (path.extension() != ".js") continue;
        const std::string name = path.filename().string();
        if (name.size() >= 11 &&
            name.compare(name.size() - 11, 11, "_FIXTURE.js") == 0) {
            continue;
        }
        bool skipped = false;
        for (const fs::path& part : path) {
            if (isSkippedDir(part, options.includeIntl)) {
                skipped = true;
                break;
            }
        }
        if (skipped) continue;
        const std::string rel = fs::relative(path, base).generic_string();
        if (!options.filter.empty() &&
            rel.find(options.filter) == std::string::npos) {
            continue;
        }
        files.push_back(path.generic_string());
    }
    std::sort(files.begin(), files.end());
    if (options.limit > 0 && files.size() > options.limit) {
        files.resize(options.limit);
    }

    std::vector<neo::test262::TestResult> results;
    results.reserve(files.size());
    if (options.noIsolation) {
        runInProcess(options, files, testBase, results);
    } else {
        const std::string executable = fs::absolute(argv[0]).string();
        size_t counter = 0;
        for (size_t start = 0; start < files.size(); start += options.chunkSize) {
            const size_t end = std::min(start + options.chunkSize, files.size());
            const std::vector<std::string> chunk(files.begin() +
                                                     static_cast<std::ptrdiff_t>(start),
                                                 files.begin() +
                                                     static_cast<std::ptrdiff_t>(end));
            runChunk(executable, options, testBase, chunk, results, counter);
            if (!options.verbose) {
                std::cerr << "\r" << results.size() << "/" << files.size()
                          << std::flush;
            }
        }
        if (!options.verbose) std::cerr << "\r";
    }

    reportResults(options, results);
    for (const auto& result : results) {
        if (result.status == neo::test262::Status::Fail) return 1;
    }
    return 0;
}
