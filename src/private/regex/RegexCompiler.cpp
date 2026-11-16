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
            error(node, "compiler: backreferences not implemented until R2");
            break;
        case NodeKind::PropertyEscape:
            error(node,
                  "compiler: property escapes not implemented until R3");
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
    for (const ClassItem& item : node.items) {
        switch (item.kind) {
            case ClassItem::Kind::Range:
                appendRange(table, item.from, item.to);
                break;
            case ClassItem::Kind::Escape:
                appendEscape(table, item.escape);
                break;
            case ClassItem::Kind::Property:
                error(node,
                      "compiler: property escapes not implemented until R3");
                return;
        }
    }
    normalize(table.ranges);
    program_.classes.push_back(std::move(table));
    emit(OpCode::CharClass,
         static_cast<std::uint32_t>(program_.classes.size() - 1));
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
        case Group::Kind::NegativeLookahead:
        case Group::Kind::Lookbehind:
        case Group::Kind::NegativeLookbehind:
            error(node, "compiler: lookaround not implemented until R2");
            break;
    }
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
    if (failed_) return;
    failed_ = true;
    error_ = CompileError{node.offset, std::move(message)};
}

}  // namespace neo::regex
