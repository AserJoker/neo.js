// RegexPikeVM.cpp - 线性 NFA 模拟（Pike VM）兜底（ADR-062 R6）

#include "regex/RegexPikeVM.h"

#include "regex/RegexUnicode.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <new>
#include <utility>

namespace neo::regex {

namespace {

constexpr std::size_t kUnset = std::numeric_limits<std::size_t>::max();

}  // namespace

bool supportsPikeVM(const Program& program) {
    if (program.scratchKinds.size() != program.scratchCount ||
        program.scratchLimits.size() != program.scratchCount) {
        return false;
    }
    for (const Instruction& instruction : program.instructions) {
        switch (instruction.op) {
            case OpCode::Backref:
            case OpCode::Lookahead:
            case OpCode::Lookbehind:
                return false;
            default:
                break;
        }
    }
    return true;
}

std::size_t RegexPikeVM::StateKeyHash::operator()(
    const StateKey& key) const {
    std::size_t hash = std::hash<std::uint32_t>{}(key.pc);
    for (const std::uint32_t value : key.scratch) {
        hash ^= std::hash<std::uint32_t>{}(value) + 0x9E3779B9u +
                (hash << 6) + (hash >> 2);
    }
    return hash;
}

RegexPikeVM::RegexPikeVM(const Program& program, const Subject& subject,
                         RegexHost& host)
    : program_(program), subject_(subject), host_(host) {}

bool RegexPikeVM::step() {
    ++steps_;
    if (stepLimit_ != 0 && steps_ > stepLimit_) {
        aborted_ = true;
        return false;
    }
    if ((steps_ & 1023u) == 0 && host_.shouldAbort(steps_)) {
        aborted_ = true;
        return false;
    }
    return true;
}

MatchResult RegexPikeVM::exec(ExecOptions options) {
    MatchResult result;
    result.captures.resize(program_.captureCount + 1);
    steps_ = 0;
    aborted_ = false;
    stepLimit_ = options.stepLimit;

    bool matched = false;
    Thread best;

    try {
        std::size_t start = options.startIndex;
        if (start > subject_.size()) start = subject_.size();

        // 输入边界：每个元素是一个 UTF-16 码元位置。u 模式按码点推进，
        // 与回溯 VM 的外层起点扫描保持一致。
        std::vector<std::size_t> unitPos;
        unitPos.push_back(start);
        std::size_t cursor = start;
        while (cursor < subject_.size()) {
            if (program_.flags.unicode) {
                std::size_t width = 0;
                (void)subject_.codePointAt(cursor, width);
                cursor += width;
            } else {
                ++cursor;
            }
            unitPos.push_back(cursor);
        }
        const std::size_t count = unitPos.size();

        const std::vector<std::size_t> initialCaptures(
            (static_cast<std::size_t>(program_.captureCount) + 1) * 2, kUnset);
        const std::vector<std::uint32_t> initialScratch(
            program_.scratchCount, 0);

        std::vector<Thread> clist;

        for (std::size_t index = 0; index < count; ++index) {
            const std::size_t pos = unitPos[index];

            // 非 sticky：每个起点以最低优先级加入，保证 leftmost-first。
            if (!matched && (!options.anchored || index == 0)) {
                Visited visited;
                Thread startThread;
                startThread.pc = 0;
                startThread.captures = initialCaptures;
                startThread.scratch = initialScratch;
                (void)addThread(clist, visited, std::move(startThread), pos,
                                best, matched);
            }

            std::vector<Thread> nextList;
            Visited nextVisited;
            const std::size_t nextPos =
                index + 1 < count ? unitPos[index + 1] : pos;

            for (Thread& thread : clist) {
                if (aborted_) break;
                if (thread.pc >= program_.instructions.size()) continue;
                if (!step()) break;

                const Instruction& instruction =
                    program_.instructions[thread.pc];
                bool consumed = false;
                switch (instruction.op) {
                    case OpCode::Char: {
                        if (pos < subject_.size()) {
                            if (program_.flags.unicode) {
                                std::size_t width = 0;
                                const std::uint32_t codePoint =
                                    subject_.codePointAt(pos, width);
                                consumed =
                                    matchChar(codePoint, instruction.a);
                            } else {
                                consumed = matchChar(
                                    static_cast<std::uint32_t>(subject_[pos]),
                                    instruction.a);
                            }
                        }
                        break;
                    }
                    case OpCode::CharClass: {
                        if (pos < subject_.size() &&
                            instruction.a < program_.classes.size()) {
                            if (program_.flags.unicode) {
                                std::size_t width = 0;
                                const std::uint32_t codePoint =
                                    subject_.codePointAt(pos, width);
                                consumed = classMatches(
                                    program_.classes[instruction.a], codePoint);
                            } else {
                                consumed = classMatches(
                                    program_.classes[instruction.a],
                                    static_cast<std::uint32_t>(subject_[pos]));
                            }
                        }
                        break;
                    }
                    case OpCode::Any: {
                        if (pos < subject_.size()) {
                            if (program_.flags.unicode) {
                                std::size_t width = 0;
                                const std::uint32_t codePoint =
                                    subject_.codePointAt(pos, width);
                                consumed = program_.flags.dotAll ||
                                           !isLineTerminator(codePoint);
                            } else {
                                consumed =
                                    program_.flags.dotAll ||
                                    !isLineTerminator(subject_[pos]);
                            }
                        }
                        break;
                    }
                    default:
                        // clist 只应包含消费指令；epsilon 指令在 addThread
                        // 中已展开。
                        continue;
                }
                if (!consumed) continue;

                // 进入下一个输入位置：进度标记（Mark 写入的位置）失效。
                for (std::size_t slot = 0;
                     slot < thread.scratch.size() &&
                     slot < program_.scratchKinds.size();
                     ++slot) {
                    if (program_.scratchKinds[slot] ==
                        ScratchKind::Progress) {
                        thread.scratch[slot] = 0;
                    }
                }
                thread.pc += 1;
                if (addThread(nextList, nextVisited, std::move(thread),
                              nextPos, best, matched)) {
                    // 找到更高优先级的 Match：截断剩余更低优先级线程。
                    break;
                }
            }

            if (aborted_) break;
            if (index + 1 >= count) break;
            if (matched && nextList.empty()) break;
            clist = std::move(nextList);
        }
    } catch (const std::bad_alloc&) {
        result.status = MatchStatus::Oom;
        return result;
    }

    if (aborted_) {
        result.status = MatchStatus::Aborted;
        return result;
    }
    if (!matched) {
        result.status = MatchStatus::NoMatch;
        return result;
    }

    result.status = MatchStatus::Matched;
    const std::size_t captureStart = best.captures[0];
    const std::size_t captureEnd = best.captures[1];
    result.start = captureStart != kUnset ? captureStart : 0;
    result.end = captureEnd != kUnset ? captureEnd : result.start;
    for (std::uint32_t i = 0; i <= program_.captureCount; ++i) {
        const std::size_t begin =
            best.captures[static_cast<std::size_t>(i) * 2];
        const std::size_t end =
            best.captures[static_cast<std::size_t>(i) * 2 + 1];
        result.captures[i] =
            Capture{begin != kUnset && end != kUnset, begin, end};
    }
    return result;
}

bool RegexPikeVM::addThread(std::vector<Thread>& list, Visited& visited,
                            Thread thread, std::size_t pos, Thread& best,
                            bool& matched) {
    std::vector<Thread> stack;
    stack.push_back(std::move(thread));

    while (!stack.empty()) {
        if (aborted_) return false;
        Thread current = std::move(stack.back());
        stack.pop_back();
        if (current.pc >= program_.instructions.size()) continue;
        if (!step()) return false;

        StateKey key;
        key.pc = current.pc;
        key.scratch = current.scratch;
        if (!visited.insert(std::move(key)).second) continue;

        const Instruction instruction = program_.instructions[current.pc];
        switch (instruction.op) {
            case OpCode::Char:
            case OpCode::CharClass:
            case OpCode::Any:
                list.push_back(std::move(current));
                break;
            case OpCode::Split: {
                Thread fallback = current;
                fallback.pc = instruction.b;
                current.pc = instruction.a;
                stack.push_back(std::move(fallback));
                stack.push_back(std::move(current));
                break;
            }
            case OpCode::Jmp:
                current.pc = instruction.a;
                stack.push_back(std::move(current));
                break;
            case OpCode::Save:
                if (instruction.a < current.captures.size()) {
                    current.captures[instruction.a] = pos;
                }
                current.pc += 1;
                stack.push_back(std::move(current));
                break;
            case OpCode::AssertStart: {
                const bool ok =
                    pos == 0 ||
                    (program_.flags.multiline && pos > 0 &&
                     isLineTerminator(
                         static_cast<std::uint32_t>(subject_[pos - 1])));
                if (ok) {
                    current.pc += 1;
                    stack.push_back(std::move(current));
                }
                break;
            }
            case OpCode::AssertEnd: {
                const bool ok =
                    pos == subject_.size() ||
                    (program_.flags.multiline && pos < subject_.size() &&
                     isLineTerminator(subject_[pos]));
                if (ok) {
                    current.pc += 1;
                    stack.push_back(std::move(current));
                }
                break;
            }
            case OpCode::AssertWordBoundary: {
                if (isWordBefore(pos) != isWordAt(pos)) {
                    current.pc += 1;
                    stack.push_back(std::move(current));
                }
                break;
            }
            case OpCode::AssertNotWordBoundary: {
                if (isWordBefore(pos) == isWordAt(pos)) {
                    current.pc += 1;
                    stack.push_back(std::move(current));
                }
                break;
            }
            case OpCode::Mark:
                if (instruction.a < current.scratch.size()) {
                    current.scratch[instruction.a] = 1;
                }
                current.pc += 1;
                stack.push_back(std::move(current));
                break;
            case OpCode::Progress:
                if (instruction.a < current.scratch.size() &&
                    current.scratch[instruction.a] != 0) {
                    current.pc = instruction.b;
                } else {
                    current.pc += 1;
                }
                stack.push_back(std::move(current));
                break;
            case OpCode::SetCounter:
                if (instruction.a < current.scratch.size()) {
                    const std::uint32_t limit =
                        instruction.a < program_.scratchLimits.size()
                            ? program_.scratchLimits[instruction.a]
                            : instruction.b;
                    current.scratch[instruction.a] =
                        std::min(instruction.b, limit);
                }
                current.pc += 1;
                stack.push_back(std::move(current));
                break;
            case OpCode::IncCounter:
                if (instruction.a < current.scratch.size()) {
                    const std::uint32_t limit =
                        instruction.a < program_.scratchLimits.size()
                            ? program_.scratchLimits[instruction.a]
                            : std::numeric_limits<std::uint32_t>::max();
                    if (current.scratch[instruction.a] < limit) {
                        ++current.scratch[instruction.a];
                    }
                }
                current.pc += 1;
                stack.push_back(std::move(current));
                break;
            case OpCode::CounterLt:
                if (instruction.a < current.scratch.size() &&
                    current.scratch[instruction.a] < instruction.b) {
                    current.pc = instruction.c;
                } else {
                    current.pc += 1;
                }
                stack.push_back(std::move(current));
                break;
            case OpCode::Match:
                best = std::move(current);
                matched = true;
                return true;
            case OpCode::Backref:
            case OpCode::Lookahead:
            case OpCode::Lookbehind:
                // supportsPikeVM 已排除；防御性忽略。
                break;
        }
    }
    return false;
}

bool RegexPikeVM::matchChar(std::uint32_t value,
                            std::uint32_t pattern) const {
    if (value == pattern) return true;
    if (!program_.flags.ignoreCase) return false;
    if (program_.flags.unicode) {
        return simpleFold(value) == simpleFold(pattern);
    }
    return asciiUpper(value) == asciiUpper(pattern);
}

bool RegexPikeVM::classMatches(const CharClassTable& table,
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

bool RegexPikeVM::inRanges(const std::vector<CharRange>& ranges,
                           std::uint32_t codePoint) {
    for (const CharRange& range : ranges) {
        if (codePoint >= range.from && codePoint <= range.to) return true;
    }
    return false;
}

std::uint32_t RegexPikeVM::asciiUpper(std::uint32_t codePoint) {
    if (codePoint >= 'a' && codePoint <= 'z') return codePoint - 32;
    return codePoint;
}

std::uint32_t RegexPikeVM::asciiLower(std::uint32_t codePoint) {
    if (codePoint >= 'A' && codePoint <= 'Z') return codePoint + 32;
    return codePoint;
}

bool RegexPikeVM::isLineTerminator(std::uint32_t codePoint) {
    return codePoint == 0x0A || codePoint == 0x0D || codePoint == 0x2028 ||
           codePoint == 0x2029;
}

bool RegexPikeVM::isWord(std::uint32_t value) const {
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

bool RegexPikeVM::isWordAt(std::size_t pos) const {
    if (pos >= subject_.size()) return false;
    if (!program_.flags.unicode) {
        return isWord(static_cast<std::uint32_t>(subject_[pos]));
    }
    std::size_t width = 0;
    return isWord(subject_.codePointAt(pos, width));
}

bool RegexPikeVM::isWordBefore(std::size_t pos) const {
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
