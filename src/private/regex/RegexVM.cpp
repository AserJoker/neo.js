// RegexVM.cpp - 回溯匹配执行器（ADR-062）

#include "regex/RegexVM.h"

#include "regex/RegexUnicode.h"

#include <limits>
#include <new>
#include <utility>

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
    stepLimit_ = options.stepLimit;

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

    std::size_t matchEnd = 0;
    if (!execute(program_.instructions, program_.classes,
                 program_.scratchCount, captures_, start, &matchEnd)) {
        return false;
    }

    out.start = captures_[0] != kUnset ? captures_[0] : start;
    out.end = captures_[1] != kUnset ? captures_[1] : matchEnd;
    for (std::uint32_t i = 0; i <= program_.captureCount; ++i) {
        const std::size_t captureStart =
            captures_[static_cast<std::size_t>(i) * 2];
        const std::size_t captureEnd =
            captures_[static_cast<std::size_t>(i) * 2 + 1];
        out.captures[i] = Capture{captureStart != kUnset &&
                                      captureEnd != kUnset,
                                  captureStart, captureEnd};
    }
    return true;
}

bool RegexVM::runSub(const SubProgram& sub, std::size_t start,
                     std::vector<std::size_t>& captures,
                     std::size_t* matchEnd) {
    return execute(sub.instructions, sub.classes, sub.scratchCount, captures,
                   start, matchEnd);
}

bool RegexVM::execute(const std::vector<Instruction>& instructions,
                      const std::vector<CharClassTable>& classes,
                      std::uint32_t scratchCount,
                      std::vector<std::size_t>& captures, std::size_t start,
                      std::size_t* matchEnd) {
    std::vector<Frame> stack;
    std::vector<Undo> undo;
    std::vector<std::size_t> scratch(scratchCount, 0);
    std::size_t pc = 0;
    std::size_t pos = start;

    auto backtrack = [&]() -> bool {
        if (stack.empty()) return false;
        const Frame frame = stack.back();
        stack.pop_back();
        while (undo.size() > frame.undoMark) {
            const Undo entry = undo.back();
            undo.pop_back();
            captures[entry.slot] = entry.oldValue;
        }
        pc = frame.pc;
        pos = frame.pos;
        return true;
    };

    for (;;) {
        if (aborted_) return false;
        if (pc >= instructions.size()) return false;
        const Instruction instruction = instructions[pc];
        ++steps_;
        if (stepLimit_ != 0 && steps_ > stepLimit_) {
            aborted_ = true;
            return false;
        }
        if ((steps_ & 1023u) == 0 && host_.shouldAbort(steps_)) {
            aborted_ = true;
            return false;
        }

        switch (instruction.op) {
            case OpCode::Char: {
                bool matched = false;
                if (pos < subject_.size()) {
                    if (program_.flags.unicode) {
                        std::size_t width = 0;
                        const std::uint32_t codePoint =
                            subject_.codePointAt(pos, width);
                        if (matchChar(codePoint, instruction.a)) {
                            pos += width;
                            matched = true;
                        }
                    } else if (matchChar(
                                   static_cast<std::uint32_t>(subject_[pos]),
                                   instruction.a)) {
                        ++pos;
                        matched = true;
                    }
                }
                if (matched) {
                    ++pc;
                } else if (!backtrack()) {
                    return false;
                }
                break;
            }
            case OpCode::CharClass: {
                bool matched = false;
                if (pos < subject_.size()) {
                    if (program_.flags.unicode) {
                        std::size_t width = 0;
                        const std::uint32_t codePoint =
                            subject_.codePointAt(pos, width);
                        if (classMatches(classes[instruction.a], codePoint)) {
                            pos += width;
                            matched = true;
                        }
                    } else if (classMatches(
                                   classes[instruction.a],
                                   static_cast<std::uint32_t>(subject_[pos]))) {
                        ++pos;
                        matched = true;
                    }
                }
                if (matched) {
                    ++pc;
                } else if (!backtrack()) {
                    return false;
                }
                break;
            }
            case OpCode::Any: {
                bool matched = false;
                if (pos < subject_.size()) {
                    if (program_.flags.unicode) {
                        std::size_t width = 0;
                        const std::uint32_t codePoint =
                            subject_.codePointAt(pos, width);
                        if (program_.flags.dotAll ||
                            !isLineTerminator(codePoint)) {
                            pos += width;
                            matched = true;
                        }
                    } else if (program_.flags.dotAll ||
                               !isLineTerminator(subject_[pos])) {
                        ++pos;
                        matched = true;
                    }
                }
                if (matched) {
                    ++pc;
                } else if (!backtrack()) {
                    return false;
                }
                break;
            }
            case OpCode::Split:
                stack.push_back(Frame{instruction.b, pos, undo.size()});
                pc = instruction.a;
                break;
            case OpCode::Jmp:
                pc = instruction.a;
                break;
            case OpCode::Save:
                undo.push_back(Undo{instruction.a, captures[instruction.a]});
                captures[instruction.a] = pos;
                ++pc;
                break;
            case OpCode::AssertStart:
                if (pos == 0 ||
                    (program_.flags.multiline &&
                     isLineTerminator(subject_[pos - 1]))) {
                    ++pc;
                } else if (!backtrack()) {
                    return false;
                }
                break;
            case OpCode::AssertEnd:
                if (pos == subject_.size() ||
                    (program_.flags.multiline && pos < subject_.size() &&
                     isLineTerminator(subject_[pos]))) {
                    ++pc;
                } else if (!backtrack()) {
                    return false;
                }
                break;
            case OpCode::AssertWordBoundary: {
                const bool previous = isWordBefore(pos);
                const bool current = isWordAt(pos);
                if (previous != current) {
                    ++pc;
                } else if (!backtrack()) {
                    return false;
                }
                break;
            }
            case OpCode::AssertNotWordBoundary: {
                const bool previous = isWordBefore(pos);
                const bool current = isWordAt(pos);
                if (previous == current) {
                    ++pc;
                } else if (!backtrack()) {
                    return false;
                }
                break;
            }
            case OpCode::Mark:
                scratch[instruction.a] = pos;
                ++pc;
                break;
            case OpCode::Progress:
                if (scratch[instruction.a] == pos) {
                    pc = instruction.b;
                } else {
                    ++pc;
                }
                break;
            case OpCode::SetCounter:
                scratch[instruction.a] = instruction.b;
                ++pc;
                break;
            case OpCode::IncCounter:
                ++scratch[instruction.a];
                ++pc;
                break;
            case OpCode::CounterLt:
                if (scratch[instruction.a] < instruction.b) {
                    pc = instruction.c;
                } else {
                    ++pc;
                }
                break;
            case OpCode::Backref: {
                const std::size_t group = instruction.a;
                bool ok = true;
                if (group == 0 || group > program_.captureCount) {
                    ok = false;
                } else {
                    const std::size_t begin = captures[group * 2];
                    const std::size_t end = captures[group * 2 + 1];
                    // 未参与匹配的捕获组按空串处理。
                    if (begin != kUnset && end != kUnset) {
                        const std::size_t length = end - begin;
                        if (pos + length > subject_.size()) {
                            ok = false;
                        } else {
                            for (std::size_t i = 0; i < length; ++i) {
                                if (!matchChar(subject_[pos + i],
                                               subject_[begin + i])) {
                                    ok = false;
                                    break;
                                }
                            }
                            if (ok) pos += length;
                        }
                    }
                }
                if (ok) {
                    ++pc;
                } else if (!backtrack()) {
                    return false;
                }
                break;
            }
            case OpCode::Lookahead: {
                const SubProgram& sub =
                    program_.subPrograms[instruction.a];
                const bool negate = instruction.b != 0;
                std::vector<std::size_t> snapshot = captures;
                std::size_t subEnd = 0;
                const bool matched = runSub(sub, pos, captures, &subEnd);
                if (aborted_) return false;
                if (negate) {
                    captures = std::move(snapshot);
                    if (matched) {
                        if (!backtrack()) return false;
                    } else {
                        ++pc;
                    }
                } else if (matched) {
                    ++pc;
                } else {
                    captures = std::move(snapshot);
                    if (!backtrack()) return false;
                }
                break;
            }
            case OpCode::Lookbehind: {
                const SubProgram& sub =
                    program_.subPrograms[instruction.a];
                const bool negate = instruction.b != 0;
                const std::vector<std::size_t> snapshot = captures;
                bool found = false;
                std::vector<std::size_t> foundCaptures;

                // R2：从最左候选起点枚举，要求子匹配恰好结束于 pos。
                // 变长后瞻因此可用；反向匹配优化留 R6。
                std::size_t candidate = 0;
                while (candidate <= pos) {
                    captures = snapshot;
                    std::size_t subEnd = 0;
                    if (runSub(sub, candidate, captures, &subEnd)) {
                        if (aborted_) return false;
                        if (subEnd == pos) {
                            found = true;
                            foundCaptures = captures;
                            break;
                        }
                    } else if (aborted_) {
                        return false;
                    }
                    if (candidate >= pos) break;
                    if (program_.flags.unicode) {
                        std::size_t width = 0;
                        (void)subject_.codePointAt(candidate, width);
                        candidate += width;
                    } else {
                        ++candidate;
                    }
                }

                if (negate) {
                    captures = snapshot;
                    if (found) {
                        if (!backtrack()) return false;
                    } else {
                        ++pc;
                    }
                } else if (found) {
                    captures = std::move(foundCaptures);
                    ++pc;
                } else {
                    captures = snapshot;
                    if (!backtrack()) return false;
                }
                break;
            }
            case OpCode::Match:
                *matchEnd = pos;
                return true;
        }
    }
}

bool RegexVM::matchChar(std::uint32_t value, std::uint32_t pattern) const {
    if (value == pattern) return true;
    if (!program_.flags.ignoreCase) return false;
    if (program_.flags.unicode) {
        return simpleFold(value) == simpleFold(pattern);
    }
    return asciiUpper(value) == asciiUpper(pattern);
}

bool RegexVM::classMatches(const CharClassTable& table,
                           std::uint32_t codePoint) const {
    bool matched = inRanges(table.ranges, codePoint);
    if (!matched && program_.flags.ignoreCase) {
        if (program_.flags.unicode) {
            const FoldPair* begin = foldOrbitBegin(codePoint);
            const FoldPair* end = foldOrbitEnd(codePoint);
            for (const FoldPair* it = begin; it != end; ++it) {
                if (inRanges(table.ranges, it->to)) {
                    matched = true;
                    break;
                }
            }
        } else {
            matched = inRanges(table.ranges, asciiUpper(codePoint)) ||
                      inRanges(table.ranges, asciiLower(codePoint));
        }
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

bool RegexVM::isWord(std::uint32_t value) const {
    const auto asciiWord = [](std::uint32_t codePoint) {
        return (codePoint >= u'A' && codePoint <= u'Z') ||
               (codePoint >= u'a' && codePoint <= u'z') ||
               (codePoint >= u'0' && codePoint <= u'9') ||
               codePoint == u'_';
    };
    if (asciiWord(value)) return true;
    if (program_.flags.unicode && program_.flags.ignoreCase) {
        return asciiWord(simpleFold(value));
    }
    return false;
}

bool RegexVM::isWordAt(std::size_t pos) const {
    if (pos >= subject_.size()) return false;
    if (!program_.flags.unicode) {
        return isWord(static_cast<std::uint32_t>(subject_[pos]));
    }
    std::size_t width = 0;
    return isWord(subject_.codePointAt(pos, width));
}

bool RegexVM::isWordBefore(std::size_t pos) const {
    if (pos == 0) return false;
    if (!program_.flags.unicode) {
        return isWord(static_cast<std::uint32_t>(subject_[pos - 1]));
    }
    std::size_t start = pos - 1;
    if (start > 0 && subject_[start] >= 0xDC00 && subject_[start] <= 0xDFFF &&
        subject_[start - 1] >= 0xD800 && subject_[start - 1] <= 0xDBFF) {
        --start;
    }
    std::size_t width = 0;
    return isWord(subject_.codePointAt(start, width));
}

}  // namespace neo::regex
