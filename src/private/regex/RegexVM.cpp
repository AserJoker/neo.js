// RegexVM.cpp - 回溯匹配执行器（ADR-062）

#include "regex/RegexVM.h"

#include <limits>
#include <new>

namespace neo::regex {

namespace {

constexpr std::size_t kUnset = std::numeric_limits<std::size_t>::max();

}  // namespace

RegexVM::RegexVM(const Program& program, const Subject& subject,
                 RegexHost& host)
    : program_(program), subject_(subject), host_(host) {}

MatchResult RegexVM::exec(ExecOptions options) {
    MatchResult result;
    result.captures.resize(program_.captureCount + 1);
    steps_ = 0;
    aborted_ = false;

    try {
        std::size_t start = options.startIndex;
        if (start > subject_.size()) start = subject_.size();
        for (;;) {
            if (runAt(start, result)) {
                result.status = MatchStatus::Matched;
                return result;
            }
            if (aborted_) {
                result.status = MatchStatus::Aborted;
                return result;
            }
            if (options.anchored || start >= subject_.size()) break;
            if (program_.flags.unicode) {
                std::size_t width = 0;
                (void)subject_.codePointAt(start, width);
                start += width;
            } else {
                ++start;
            }
        }
    } catch (const std::bad_alloc&) {
        result.status = MatchStatus::Oom;
        return result;
    }

    result.status = MatchStatus::NoMatch;
    return result;
}

bool RegexVM::runAt(std::size_t start, MatchResult& out) {
    captures_.assign((static_cast<std::size_t>(program_.captureCount) + 1) * 2,
                     kUnset);
    undo_.clear();
    stack_.clear();
    scratch_.assign(program_.scratchCount, 0);
    pc_ = 0;
    pos_ = start;

    for (;;) {
        if (pc_ >= program_.instructions.size()) return false;
        const Instruction& instruction = program_.instructions[pc_];
        ++steps_;
        if ((steps_ & 1023u) == 0 && host_.shouldAbort(steps_)) {
            aborted_ = true;
            return false;
        }

        switch (instruction.op) {
            case OpCode::Char:
                if (pos_ < subject_.size() &&
                    matchChar(subject_[pos_], instruction.a)) {
                    ++pos_;
                    ++pc_;
                } else if (!backtrack()) {
                    return false;
                }
                break;
            case OpCode::CharClass:
                if (pos_ < subject_.size() &&
                    classMatches(program_.classes[instruction.a],
                                 subject_[pos_])) {
                    ++pos_;
                    ++pc_;
                } else if (!backtrack()) {
                    return false;
                }
                break;
            case OpCode::Any:
                if (pos_ < subject_.size() &&
                    (program_.flags.dotAll ||
                     !isLineTerminator(subject_[pos_]))) {
                    ++pos_;
                    ++pc_;
                } else if (!backtrack()) {
                    return false;
                }
                break;
            case OpCode::Split:
                stack_.push_back(Frame{instruction.b, pos_, undo_.size()});
                pc_ = instruction.a;
                break;
            case OpCode::Jmp:
                pc_ = instruction.a;
                break;
            case OpCode::Save:
                undo_.push_back(Undo{instruction.a, captures_[instruction.a]});
                captures_[instruction.a] = pos_;
                ++pc_;
                break;
            case OpCode::AssertStart:
                if (pos_ == 0 ||
                    (program_.flags.multiline &&
                     isLineTerminator(subject_[pos_ - 1]))) {
                    ++pc_;
                } else if (!backtrack()) {
                    return false;
                }
                break;
            case OpCode::AssertEnd:
                if (pos_ == subject_.size() ||
                    (program_.flags.multiline && pos_ < subject_.size() &&
                     isLineTerminator(subject_[pos_]))) {
                    ++pc_;
                } else if (!backtrack()) {
                    return false;
                }
                break;
            case OpCode::AssertWordBoundary: {
                const bool previous =
                    pos_ > 0 && isWord(subject_[pos_ - 1]);
                const bool current =
                    pos_ < subject_.size() && isWord(subject_[pos_]);
                if (previous != current) {
                    ++pc_;
                } else if (!backtrack()) {
                    return false;
                }
                break;
            }
            case OpCode::AssertNotWordBoundary: {
                const bool previous =
                    pos_ > 0 && isWord(subject_[pos_ - 1]);
                const bool current =
                    pos_ < subject_.size() && isWord(subject_[pos_]);
                if (previous == current) {
                    ++pc_;
                } else if (!backtrack()) {
                    return false;
                }
                break;
            }
            case OpCode::Mark:
                scratch_[instruction.a] = pos_;
                ++pc_;
                break;
            case OpCode::Progress:
                if (scratch_[instruction.a] == pos_) {
                    pc_ = instruction.b;
                } else {
                    ++pc_;
                }
                break;
            case OpCode::SetCounter:
                scratch_[instruction.a] = instruction.b;
                ++pc_;
                break;
            case OpCode::IncCounter:
                ++scratch_[instruction.a];
                ++pc_;
                break;
            case OpCode::CounterLt:
                if (scratch_[instruction.a] < instruction.b) {
                    pc_ = instruction.c;
                } else {
                    ++pc_;
                }
                break;
            case OpCode::Match: {
                const std::size_t matchStart =
                    captures_[0] != kUnset ? captures_[0] : start;
                const std::size_t matchEnd =
                    captures_[1] != kUnset ? captures_[1] : pos_;
                out.start = matchStart;
                out.end = matchEnd;
                for (std::uint32_t i = 0; i <= program_.captureCount; ++i) {
                    const std::size_t captureStart =
                        captures_[static_cast<std::size_t>(i) * 2];
                    const std::size_t captureEnd =
                        captures_[static_cast<std::size_t>(i) * 2 + 1];
                    out.captures[i] = Capture{
                        captureStart != kUnset && captureEnd != kUnset,
                        captureStart, captureEnd};
                }
                return true;
            }
        }
    }
}

bool RegexVM::backtrack() {
    if (stack_.empty()) return false;
    const Frame frame = stack_.back();
    stack_.pop_back();
    while (undo_.size() > frame.undoMark) {
        const Undo undo = undo_.back();
        undo_.pop_back();
        captures_[undo.slot] = undo.oldValue;
    }
    pc_ = frame.pc;
    pos_ = frame.pos;
    return true;
}

bool RegexVM::matchChar(char16_t value, std::uint32_t pattern) const {
    const std::uint32_t codePoint = static_cast<std::uint32_t>(value);
    if (codePoint == pattern) return true;
    if (!program_.flags.ignoreCase) return false;
    return asciiUpper(codePoint) == asciiUpper(pattern);
}

bool RegexVM::classMatches(const CharClassTable& table,
                           char16_t value) const {
    const std::uint32_t codePoint = static_cast<std::uint32_t>(value);
    bool matched = inRanges(table.ranges, codePoint);
    if (!matched && program_.flags.ignoreCase) {
        matched = inRanges(table.ranges, asciiUpper(codePoint)) ||
                  inRanges(table.ranges, asciiLower(codePoint));
    }
    return table.negated ? !matched : matched;
}

bool RegexVM::inRanges(const std::vector<CharRange>& ranges,
                       std::uint32_t codePoint) {
    for (const CharRange& range : ranges) {
        if (codePoint >= range.from && codePoint <= range.to) return true;
    }
    return false;
}

std::uint32_t RegexVM::asciiUpper(std::uint32_t codePoint) {
    if (codePoint >= 'a' && codePoint <= 'z') return codePoint - 32;
    return codePoint;
}

std::uint32_t RegexVM::asciiLower(std::uint32_t codePoint) {
    if (codePoint >= 'A' && codePoint <= 'Z') return codePoint + 32;
    return codePoint;
}

bool RegexVM::isLineTerminator(std::uint32_t codePoint) {
    return codePoint == 0x0A || codePoint == 0x0D || codePoint == 0x2028 ||
           codePoint == 0x2029;
}

bool RegexVM::isWord(char16_t value) {
    return (value >= u'A' && value <= u'Z') ||
           (value >= u'a' && value <= u'z') ||
           (value >= u'0' && value <= u'9') || value == u'_';
}

}  // namespace neo::regex
