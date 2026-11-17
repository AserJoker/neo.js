// RegexFuzz.cpp - 正则 Parser/Compiler/VM 的 libFuzzer 目标（ADR-062 R6）
//
// 输入格式：首字节为 flags 位集，其余字节对半分为 pattern 与 subject。
// 字节先映射到受限字母表，保证输入始终是合法 ASCII，从而把 fuzz 预算集中
// 在语法/编译/匹配路径上，而不是无效 UTF-8 解码。
//
// 构建（Clang）：
//   cmake -DNEO_BUILD_FUZZERS=ON -DCMAKE_CXX_COMPILER=clang++ ...
//   ninja regex_fuzz
//   ./bin/regex_fuzz -max_total_time=60

#include "regex/RegexEngine.h"
#include "regex/RegexFlags.h"
#include "regex/RegexHost.h"
#include "regex/RegexMatch.h"
#include "regex/RegexProgram.h"
#include "regex/RegexSubject.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace {

class FuzzUnicodeTables final : public neo::regex::UnicodeTables {};

class FuzzHost final : public neo::regex::RegexHost {
public:
    const neo::regex::UnicodeTables& unicode() const override { return tables_; }
    bool shouldAbort(std::uint64_t) override { return false; }

private:
    FuzzUnicodeTables tables_;
};

std::string mapBytes(const std::uint8_t* data, std::size_t size,
                     const char* alphabet) {
    const std::size_t alphabetSize = std::char_traits<char>::length(alphabet);
    std::string out;
    out.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(alphabet[data[i] % alphabetSize]);
    }
    return out;
}

std::string makeFlags(std::uint8_t bits) {
    std::string flags;
    if ((bits & 0x01u) != 0) flags.push_back('d');
    if ((bits & 0x02u) != 0) flags.push_back('g');
    if ((bits & 0x04u) != 0) flags.push_back('i');
    if ((bits & 0x08u) != 0) flags.push_back('m');
    if ((bits & 0x10u) != 0) flags.push_back('s');
    if ((bits & 0x20u) != 0) flags.push_back('y');
    if ((bits & 0x40u) != 0) {
        flags.push_back('u');
    } else if ((bits & 0x80u) != 0) {
        flags.push_back('v');
    }
    return flags;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    if (size < 2) return 0;

    const std::uint8_t flagBits = data[0];
    const std::uint8_t* payload = data + 1;
    const std::size_t payloadSize = size - 1;
    const std::size_t patternSize = payloadSize / 2;

    static constexpr char kPatternAlphabet[] =
        "ab()[]{}|*+?\\^$.-0123456789dDsSwWpPq&:=,<>#!%:;@~'`\" \n";
    static constexpr char kSubjectAlphabet[] = "ab01- \n";

    const std::string pattern =
        mapBytes(payload, patternSize, kPatternAlphabet);
    const std::string text = mapBytes(payload + patternSize,
                                      payloadSize - patternSize,
                                      kSubjectAlphabet);

    auto parsed = neo::regex::parseFlags(makeFlags(flagBits));
    if (!parsed.ok()) return 0;

    auto compiled = neo::regex::compilePattern(pattern, parsed.flags);
    if (!compiled.ok()) return 0;

    static FuzzHost host;
    static neo::regex::RegexEngine engine(host);

    const neo::regex::Subject subject = neo::regex::Subject::fromAscii(text);
    neo::regex::ExecOptions options;
    options.stepLimit = 200000;
    (void)engine.exec(*compiled.program, subject, options);
    return 0;
}
