// RegexCompiler.cpp - RegexAST -> Program（ADR-062）

#include "regex/RegexCompiler.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace neo::regex {

namespace {

constexpr std::uint32_t kMaxCodePoint = 0x10FFFFu;

}  // namespace

CompileResult RegexCompiler::compile(const ParseResult& parsed,
                                     std::string_view source, Flags flags,
                                     Dialect dialect) {
    program_.source = std::string(source);
    program_.flags = flags;
    program_.dialect = dialect;
    program_.captureCount = parsed.captureCount;
    program_.namedGroups = parsed.namedGroups;

    emit(OpCode::Save, 0);
    if (parsed.root) compileNode(*parsed.root);
    if (failed_) return CompileResult{nullptr, error_};
    emit(OpCode::Save, 1);
    emit(OpCode::Match);
    program_.scratchCount = scratchCount_;
    return CompileResult{std::make_shared<Program>(std::move(program_)),
                         std::nullopt};
}

std::uint32_t RegexCompiler::emit(OpCode op, std::uint32_t a,
                                  std::uint32_t b, std::uint32_t c) {
    program_.instructions.push_back(Instruction{op, a, b, c});
    return static_cast<std::uint32_t>(program_.instructions.size() - 1);
}

void RegexCompiler::patchA(std::uint32_t index, std::uint32_t value) {
    program_.instructions[index].a = value;
}

void RegexCompiler::patchB(std::uint32_t index, std::uint32_t value) {
    program_.instructions[index].b = value;
}

void RegexCompiler::patchC(std::uint32_t index, std::uint32_t value) {
    program_.instructions[index].c = value;
}

void RegexCompiler::compileNode(const Node& node) {
    if (failed_) return;
    switch (node.kind) {
        case NodeKind::Disjunction:
            compileDisjunction(static_cast<const Disjunction&>(node));
            break;
        case NodeKind::Alternative:
            compileAlternative(static_cast<const Alternative&>(node));
            break;
        case NodeKind::Empty:
            break;
        case NodeKind::Character:
            compileCharacter(static_cast<const Character&>(node));
            break;
        case NodeKind::Dot:
            emit(OpCode::Any);
            break;
        case NodeKind::CharacterClass:
            compileCharacterClass(static_cast<const CharacterClass&>(node));
            break;
        case NodeKind::Group:
            compileGroup(static_cast<const Group&>(node));
            break;
        case NodeKind::Quantifier:
            compileQuantifier(static_cast<const Quantifier&>(node));
            break;
        case NodeKind::Assertion:
            compileAssertion(static_cast<const Assertion&>(node));
            break;
        case NodeKind::Backreference:
            compileBackreference(static_cast<const Backreference&>(node));
            break;
        case NodeKind::PropertyEscape:
            compilePropertyEscape(static_cast<const PropertyEscape&>(node));
            break;
    }
}

void RegexCompiler::compileDisjunction(const Disjunction& node) {
    const auto& alternatives = node.alternatives;
    if (alternatives.empty()) return;

    std::vector<std::uint32_t> jumps;
    for (std::size_t i = 0; i < alternatives.size(); ++i) {
        if (i + 1 < alternatives.size()) {
            const std::uint32_t split = emit(OpCode::Split, 0, 0);
            const std::uint32_t body = here();
            patchA(split, body);
            compileNode(*alternatives[i]);
            if (failed_) return;
            jumps.push_back(emit(OpCode::Jmp, 0));
            const std::uint32_t nextAlternative = here();
            patchB(split, nextAlternative);
        } else {
            compileNode(*alternatives[i]);
            if (failed_) return;
        }
    }
    const std::uint32_t end = here();
    for (std::uint32_t jump : jumps) patchA(jump, end);
}

void RegexCompiler::compileAlternative(const Alternative& node) {
    for (const NodePtr& term : node.terms) {
        if (!term) continue;
        compileNode(*term);
        if (failed_) return;
    }
}

void RegexCompiler::compileCharacter(const Character& node) {
    emit(OpCode::Char, node.codePoint);
}

void RegexCompiler::compileCharacterClass(const CharacterClass& node) {
    CharClassTable table;
    table.negated = node.negated;
    collectClassRanges(node, table.ranges);
    if (failed_) return;
    normalize(table.ranges);
    program_.classes.push_back(std::move(table));
    emit(OpCode::CharClass,
         static_cast<std::uint32_t>(program_.classes.size() - 1));
}

void RegexCompiler::collectClassRanges(const CharacterClass& node,
                                       std::vector<CharRange>& out) {
    if (node.left && node.right) {
        std::vector<CharRange> left;
        std::vector<CharRange> right;
        collectClassRanges(*node.left, left);
        if (failed_) return;
        collectClassRanges(*node.right, right);
        if (failed_) return;
        switch (node.setOp) {
            case CharacterClass::SetOp::Union:
                out.insert(out.end(), left.begin(), left.end());
                out.insert(out.end(), right.begin(), right.end());
                break;
            case CharacterClass::SetOp::Intersection: {
                std::vector<CharRange> result =
                    intersectRanges(std::move(left), std::move(right));
                out.insert(out.end(), result.begin(), result.end());
                break;
            }
            case CharacterClass::SetOp::Subtraction: {
                std::vector<CharRange> result =
                    subtractRanges(std::move(left), std::move(right));
                out.insert(out.end(), result.begin(), result.end());
                break;
            }
        }
        return;
    }

    for (const ClassItem& item : node.items) {
        switch (item.kind) {
            case ClassItem::Kind::Range:
                out.push_back(CharRange{item.from, item.to});
                break;
            case ClassItem::Kind::Escape: {
                CharClassTable temp;
                appendEscape(temp, item.escape);
                out.insert(out.end(), temp.ranges.begin(), temp.ranges.end());
                break;
            }
            case ClassItem::Kind::Property: {
                UnicodeProperty property;
                if (!resolveProperty(item.property, property)) {
                    error(item.offset, "Invalid property name");
                    return;
                }
                std::vector<CharRange> ranges;
                ranges.reserve(property.count);
                for (std::size_t i = 0; i < property.count; ++i) {
                    ranges.push_back(CharRange{property.ranges[i].from,
                                               property.ranges[i].to});
                }
                if (item.negated) {
                    appendComplement(out, std::move(ranges));
                } else {
                    out.insert(out.end(), ranges.begin(), ranges.end());
                }
                break;
            }
            case ClassItem::Kind::Nested: {
                if (!item.nested) break;
                std::vector<CharRange> ranges;
                collectClassRanges(*item.nested, ranges);
                if (failed_) return;
                if (item.nested->negated) {
                    appendComplement(out, std::move(ranges));
                } else {
                    out.insert(out.end(), ranges.begin(), ranges.end());
                }
                break;
            }
            case ClassItem::Kind::String:
                if (item.chars.size() != 1) {
                    error(item.offset,
                          "properties of strings not implemented");
                    return;
                }
                out.push_back(CharRange{item.chars[0], item.chars[0]});
                break;
        }
    }
}

std::vector<CharRange> RegexCompiler::intersectRanges(
    std::vector<CharRange> left, std::vector<CharRange> right) {
    normalize(left);
    normalize(right);
    std::vector<CharRange> out;
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < left.size() && j < right.size()) {
        const std::uint32_t from =
            left[i].from > right[j].from ? left[i].from : right[j].from;
        const std::uint32_t to =
            left[i].to < right[j].to ? left[i].to : right[j].to;
        if (from <= to) out.push_back(CharRange{from, to});
        if (left[i].to < right[j].to) {
            ++i;
        } else {
            ++j;
        }
    }
    return out;
}

std::vector<CharRange> RegexCompiler::subtractRanges(
    std::vector<CharRange> left, std::vector<CharRange> right) {
    normalize(left);
    normalize(right);
    std::vector<CharRange> out;
    for (const CharRange& range : left) {
        std::vector<CharRange> pieces{range};
        for (const CharRange& cut : right) {
            std::vector<CharRange> next;
            for (const CharRange& piece : pieces) {
                if (cut.to < piece.from || cut.from > piece.to) {
                    next.push_back(piece);
                    continue;
                }
                if (piece.from < cut.from) {
                    next.push_back(CharRange{piece.from, cut.from - 1});
                }
                if (piece.to > cut.to) {
                    next.push_back(CharRange{cut.to + 1, piece.to});
                }
            }
            pieces = std::move(next);
            if (pieces.empty()) break;
        }
        out.insert(out.end(), pieces.begin(), pieces.end());
    }
    return out;
}

void RegexCompiler::compileGroup(const Group& node) {
    if (!node.body) return;
    switch (node.groupKind) {
        case Group::Kind::Capturing:
        case Group::Kind::NamedCapturing:
            emit(OpCode::Save, node.captureIndex * 2);
            compileNode(*node.body);
            if (failed_) return;
            emit(OpCode::Save, node.captureIndex * 2 + 1);
            break;
        case Group::Kind::NonCapturing:
            compileNode(*node.body);
            break;
        case Group::Kind::Lookahead:
            compileLookaround(node, false, false);
            break;
        case Group::Kind::NegativeLookahead:
            compileLookaround(node, false, true);
            break;
        case Group::Kind::Lookbehind:
            compileLookaround(node, true, false);
            break;
        case Group::Kind::NegativeLookbehind:
            compileLookaround(node, true, true);
            break;
    }
}

void RegexCompiler::compileLookaround(const Group& node, bool behind,
                                      bool negate) {
    if (!node.body) return;

    // 子程序拥有独立的指令/字符类/scratch，捕获槽与主程序共享。
    std::vector<Instruction> savedInstructions =
        std::move(program_.instructions);
    std::vector<CharClassTable> savedClasses = std::move(program_.classes);
    const std::uint32_t savedScratch = scratchCount_;

    program_.instructions.clear();
    program_.classes.clear();
    scratchCount_ = 0;
    compileNode(*node.body);
    if (!failed_) emit(OpCode::Match);

    SubProgram sub;
    sub.instructions = std::move(program_.instructions);
    sub.classes = std::move(program_.classes);
    sub.scratchCount = scratchCount_;

    program_.instructions = std::move(savedInstructions);
    program_.classes = std::move(savedClasses);
    scratchCount_ = savedScratch;
    if (failed_) return;

    const std::uint32_t index =
        static_cast<std::uint32_t>(program_.subPrograms.size());
    program_.subPrograms.push_back(std::move(sub));
    emit(behind ? OpCode::Lookbehind : OpCode::Lookahead, index,
         negate ? 1u : 0u);
}

void RegexCompiler::compileBackreference(const Backreference& node) {
    if (node.legacyOctal) {
        emitLegacyOctal(node.digits);
        return;
    }
    std::uint32_t index = node.index;
    if (index == 0) {
        auto it = program_.namedGroups.find(node.name);
        if (it == program_.namedGroups.end()) {
            error(node, "compiler: unknown named capture");
            return;
        }
        index = it->second;
    }
    emit(OpCode::Backref, index);
}

// Annex B LegacyOctalEscapeSequence：最多 3 位八进制；首位为 4-7 时只取 2 位。
// 未被八进制消费的尾部数字按普通字符发射。
void RegexCompiler::emitLegacyOctal(const std::string& digits) {
    std::size_t count = 0;
    std::uint32_t value = 0;
    while (count < digits.size() && count < 3) {
        const char c = digits[count];
        if (c < '0' || c > '7') break;
        const std::uint32_t digit = static_cast<std::uint32_t>(c - '0');
        if (count == 0 && digit > 3) {
            value = digit;
            ++count;
            if (count < digits.size() && digits[count] >= '0' &&
                digits[count] <= '7') {
                value = value * 8u +
                        static_cast<std::uint32_t>(digits[count] - '0');
                ++count;
            }
            break;
        }
        value = value * 8u + digit;
        ++count;
    }

    if (count > 0) emit(OpCode::Char, value);
    for (std::size_t i = count; i < digits.size(); ++i) {
        emit(OpCode::Char,
             static_cast<std::uint32_t>(
                 static_cast<unsigned char>(digits[i])));
    }
}

void RegexCompiler::compilePropertyEscape(const PropertyEscape& node) {
    UnicodeProperty property;
    if (!resolveProperty(node.property, property)) {
        error(node, "Invalid property name");
        return;
    }

    CharClassTable table;
    table.negated = node.negated;
    table.ranges.reserve(property.count);
    for (std::size_t i = 0; i < property.count; ++i) {
        table.ranges.push_back(
            CharRange{property.ranges[i].from, property.ranges[i].to});
    }
    program_.classes.push_back(std::move(table));
    emit(OpCode::CharClass,
         static_cast<std::uint32_t>(program_.classes.size() - 1));
}

void RegexCompiler::appendProperty(CharClassTable& table,
                                   const ClassItem& item) {
    UnicodeProperty property;
    if (!resolveProperty(item.property, property)) {
        error(item.offset, "Invalid property name");
        return;
    }

    std::vector<CharRange> ranges;
    ranges.reserve(property.count);
    for (std::size_t i = 0; i < property.count; ++i) {
        ranges.push_back(
            CharRange{property.ranges[i].from, property.ranges[i].to});
    }
    if (item.negated) {
        appendComplement(table.ranges, std::move(ranges));
    } else {
        table.ranges.insert(table.ranges.end(), ranges.begin(), ranges.end());
    }
}

bool RegexCompiler::resolveProperty(std::string_view name,
                                    UnicodeProperty& property) {
    const std::size_t equals = name.find('=');
    if (equals == std::string_view::npos) {
        return resolveUnicodeProperty(name, property);
    }
    return resolveUnicodeProperty(name.substr(0, equals),
                                  name.substr(equals + 1), property);
}

void RegexCompiler::compileQuantifier(const Quantifier& node) {
    if (!node.atom) return;

    const std::uint32_t counter = scratchCount_++;
    const std::uint32_t progress = scratchCount_++;
    emit(OpCode::SetCounter, counter, 0);

    const std::uint32_t loop = here();
    const std::uint32_t toMandatory =
        emit(OpCode::CounterLt, counter, node.min, 0);

    const bool bounded =
        node.max != std::numeric_limits<std::uint32_t>::max();
    std::uint32_t toEnd = 0;
    std::uint32_t optional = 0;
    if (bounded) {
        const std::uint32_t toOptional =
            emit(OpCode::CounterLt, counter, node.max, 0);
        toEnd = emit(OpCode::Jmp, 0);
        optional = here();
        patchC(toOptional, optional);
    } else {
        optional = here();
    }

    const std::uint32_t split = emit(OpCode::Split, 0, 0);
    const std::uint32_t body = here();
    emit(OpCode::Mark, progress);
    compileNode(*node.atom);
    if (failed_) return;
    const std::uint32_t progressPc = emit(OpCode::Progress, progress, 0);
    emit(OpCode::IncCounter, counter);
    emit(OpCode::Jmp, loop);

    const std::uint32_t mandatory = here();
    patchC(toMandatory, mandatory);
    compileNode(*node.atom);
    if (failed_) return;
    emit(OpCode::IncCounter, counter);
    emit(OpCode::Jmp, loop);

    const std::uint32_t end = here();
    patchA(split, node.greedy ? body : end);
    patchB(split, node.greedy ? end : body);
    patchB(progressPc, end);
    if (bounded) patchA(toEnd, end);
}

void RegexCompiler::compileAssertion(const Assertion& node) {
    switch (node.assertion) {
        case Assertion::Kind::Start:
            emit(OpCode::AssertStart);
            break;
        case Assertion::Kind::End:
            emit(OpCode::AssertEnd);
            break;
        case Assertion::Kind::WordBoundary:
            emit(OpCode::AssertWordBoundary);
            break;
        case Assertion::Kind::NotWordBoundary:
            emit(OpCode::AssertNotWordBoundary);
            break;
    }
}

void RegexCompiler::appendRange(CharClassTable& table, std::uint32_t from,
                                std::uint32_t to) {
    table.ranges.push_back(CharRange{from, to});
}

void RegexCompiler::appendEscape(CharClassTable& table,
                                 ClassItem::EscapeKind kind) {
    const std::vector<CharRange> digit = {{0x30, 0x39}};
    const std::vector<CharRange> word = {
        {0x30, 0x39}, {0x41, 0x5A}, {0x5F, 0x5F}, {0x61, 0x7A}};
    const std::vector<CharRange> space = {
        {0x09, 0x0D},   {0x20, 0x20},   {0xA0, 0xA0},
        {0x1680, 0x1680}, {0x2000, 0x200A}, {0x2028, 0x2029},
        {0x202F, 0x202F}, {0x205F, 0x205F}, {0x3000, 0x3000},
        {0xFEFF, 0xFEFF}};

    switch (kind) {
        case ClassItem::EscapeKind::Digit:
            table.ranges.insert(table.ranges.end(), digit.begin(),
                                digit.end());
            break;
        case ClassItem::EscapeKind::NotDigit:
            appendComplement(table.ranges, digit);
            break;
        case ClassItem::EscapeKind::Word:
            table.ranges.insert(table.ranges.end(), word.begin(),
                                word.end());
            break;
        case ClassItem::EscapeKind::NotWord:
            appendComplement(table.ranges, word);
            break;
        case ClassItem::EscapeKind::Space:
            table.ranges.insert(table.ranges.end(), space.begin(),
                                space.end());
            break;
        case ClassItem::EscapeKind::NotSpace:
            appendComplement(table.ranges, space);
            break;
    }
}

void RegexCompiler::appendComplement(std::vector<CharRange>& out,
                                     std::vector<CharRange> base) {
    normalize(base);
    std::uint64_t next = 0;
    for (const CharRange& range : base) {
        if (static_cast<std::uint64_t>(range.from) > next) {
            out.push_back(CharRange{static_cast<std::uint32_t>(next),
                                    range.from - 1});
        }
        next = static_cast<std::uint64_t>(range.to) + 1;
    }
    if (next <= kMaxCodePoint) {
        out.push_back(CharRange{static_cast<std::uint32_t>(next),
                                kMaxCodePoint});
    }
}

void RegexCompiler::normalize(std::vector<CharRange>& ranges) {
    if (ranges.empty()) return;
    std::sort(ranges.begin(), ranges.end(),
              [](const CharRange& a, const CharRange& b) {
                  return a.from < b.from;
              });
    std::vector<CharRange> merged;
    merged.reserve(ranges.size());
    for (const CharRange& range : ranges) {
        if (merged.empty() ||
            static_cast<std::uint64_t>(range.from) >
                static_cast<std::uint64_t>(merged.back().to) + 1) {
            merged.push_back(range);
        } else if (range.to > merged.back().to) {
            merged.back().to = range.to;
        }
    }
    ranges = std::move(merged);
}

void RegexCompiler::error(const Node& node, std::string message) {
    error(node.offset, std::move(message));
}

void RegexCompiler::error(std::size_t offset, std::string message) {
    if (failed_) return;
    failed_ = true;
    error_ = CompileError{offset, std::move(message)};
}

}  // namespace neo::regex
