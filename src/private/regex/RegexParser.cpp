// RegexParser.cpp - 正则 pattern 语法分析（ADR-062）

#include "regex/RegexParser.h"

#include "unicode/UnicodeClassification.h"
#include "unicode/Utf8.h"

#include <limits>
#include <memory>
#include <utility>

namespace neo::regex {

namespace {

bool isAsciiDigit(std::uint32_t cp) { return cp >= '0' && cp <= '9'; }

bool isAsciiLetter(std::uint32_t cp) {
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
}

int hexValue(std::uint32_t cp) {
    if (cp >= '0' && cp <= '9') return static_cast<int>(cp - '0');
    if (cp >= 'a' && cp <= 'f') return static_cast<int>(cp - 'a') + 10;
    if (cp >= 'A' && cp <= 'F') return static_cast<int>(cp - 'A') + 10;
    return -1;
}

bool isSyntaxCharacter(std::uint32_t cp) {
    switch (cp) {
        case '^':
        case '$':
        case '\\':
        case '.':
        case '*':
        case '+':
        case '?':
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case '|':
            return true;
        default:
            return false;
    }
}

}  // namespace

RegexParser::RegexParser(std::string_view pattern, Flags flags,
                         Dialect dialect)
    : pattern_(pattern), flags_(flags), dialect_(dialect) {}

ParseResult RegexParser::parse() {
    ParseResult result;
    NodePtr root = parseDisjunction();
    if (!failed_ && !atEnd()) {
        fail(here(), "Unmatched ')'");
    }
    validateBackrefs();
    if (failed_) {
        result.error = error_;
        return result;
    }
    result.root = std::move(root);
    result.captureCount = captureCount_;
    result.namedGroups = std::move(namedGroups_);
    return result;
}

NodePtr RegexParser::parseDisjunction() {
    auto disjunction = std::make_unique<Disjunction>(here());
    disjunction->alternatives.push_back(parseAlternative());
    if (failed_) return nullptr;
    while (eat('|')) {
        disjunction->alternatives.push_back(parseAlternative());
        if (failed_) return nullptr;
    }
    return disjunction;
}

NodePtr RegexParser::parseAlternative() {
    auto alternative = std::make_unique<Alternative>(here());
    while (!atEnd() && peek() != '|' && peek() != ')') {
        NodePtr term = parseTerm();
        if (!term) return nullptr;
        alternative->terms.push_back(std::move(term));
    }
    return alternative;
}

NodePtr RegexParser::parseTerm() {
    if (peek() == '^' || peek() == '$') return parseAssertion();
    if (peek() == '\\') {
        const std::uint32_t next = peekNext();
        if (next == 'b' || next == 'B') return parseAssertion();
    }

    NodePtr atom = parseAtom();
    if (!atom) return nullptr;

    bool assertion = atom->kind == NodeKind::Assertion;
    if (atom->kind == NodeKind::Group) {
        const auto& group = static_cast<const Group&>(*atom);
        assertion = group.groupKind == Group::Kind::Lookahead ||
                    group.groupKind == Group::Kind::NegativeLookahead ||
                    group.groupKind == Group::Kind::Lookbehind ||
                    group.groupKind == Group::Kind::NegativeLookbehind;
    }

    QuantSpec spec;
    const QuantResult quantifier = tryParseQuantifierSpec(spec);
    if (quantifier == QuantResult::Error) return nullptr;
    if (quantifier == QuantResult::Spec) {
        if (assertion) {
            fail(spec.offset, "Nothing to repeat");
            return nullptr;
        }
        auto node = std::make_unique<Quantifier>(spec.offset, spec.min,
                                                 spec.max, spec.greedy);
        node->atom = std::move(atom);
        return node;
    }
    if ((flags_.unicode || flags_.unicodeSets) && peek() == '{') {
        fail(here(), "Lone quantifier brackets");
        return nullptr;
    }
    return atom;
}

NodePtr RegexParser::parseAtom() {
    if (atEnd()) {
        fail(here(), "Unexpected end of pattern");
        return nullptr;
    }

    const std::size_t start = here();
    const std::uint32_t cp = peek();
    switch (cp) {
        case '(':
            return parseGroup();
        case '[':
            return flags_.unicodeSets ? parseVCharacterClass()
                                      : parseCharacterClass();
        case '.':
            advance();
            return std::make_unique<Dot>(start);
        case '\\':
            return parseAtomEscape();
        case '*':
        case '+':
        case '?':
            fail(start, "Nothing to repeat");
            return nullptr;
        case ')':
            fail(start, "Unmatched ')'");
            return nullptr;
        case ']':
        case '}':
        case '{':
            if (flags_.unicode || flags_.unicodeSets) {
                fail(start, "Lone quantifier brackets");
                return nullptr;
            }
            advance();
            return std::make_unique<Character>(start, cp);
        default:
            advance();
            return std::make_unique<Character>(start, cp);
    }
}

NodePtr RegexParser::parseAssertion() {
    const std::size_t start = here();
    if (eat('^')) {
        return std::make_unique<Assertion>(start, Assertion::Kind::Start);
    }
    if (eat('$')) {
        return std::make_unique<Assertion>(start, Assertion::Kind::End);
    }

    advance();  // '\'
    const std::uint32_t cp = advance();
    if (cp == 'b') {
        return std::make_unique<Assertion>(start,
                                           Assertion::Kind::WordBoundary);
    }
    if (cp == 'B') {
        return std::make_unique<Assertion>(start,
                                           Assertion::Kind::NotWordBoundary);
    }
    fail(start, "Invalid assertion");
    return nullptr;
}

NodePtr RegexParser::parseGroup() {
    const std::size_t start = here();
    advance();  // '('

    Group::Kind kind = Group::Kind::Capturing;
    std::string name;
    if (eat('?')) {
        if (eat(':')) {
            kind = Group::Kind::NonCapturing;
        } else if (eat('=')) {
            kind = Group::Kind::Lookahead;
        } else if (eat('!')) {
            kind = Group::Kind::NegativeLookahead;
        } else if (eat('<')) {
            if (eat('=')) {
                kind = Group::Kind::Lookbehind;
            } else if (eat('!')) {
                kind = Group::Kind::NegativeLookbehind;
            } else {
                kind = Group::Kind::NamedCapturing;
                if (!parseGroupName(name)) return nullptr;
            }
        } else {
            fail(here(), "Invalid group");
            return nullptr;
        }
    }

    if ((kind == Group::Kind::Lookbehind ||
         kind == Group::Kind::NegativeLookbehind) &&
        !dialect_.allowLookbehind) {
        fail(start, "Lookbehind is disabled");
        return nullptr;
    }

    auto group = std::make_unique<Group>(start, kind);
    if (kind == Group::Kind::Capturing ||
        kind == Group::Kind::NamedCapturing) {
        group->captureIndex = ++captureCount_;
        if (kind == Group::Kind::NamedCapturing) {
            if (namedGroups_.find(name) != namedGroups_.end()) {
                fail(start, "Duplicate capture group name");
                return nullptr;
            }
            namedGroups_.emplace(name, group->captureIndex);
            group->name = std::move(name);
        }
    }

    NodePtr body = parseDisjunction();
    if (!body) return nullptr;
    if (!eat(')')) {
        fail(start, "Unterminated group");
        return nullptr;
    }
    group->body = std::move(body);
    return group;
}

NodePtr RegexParser::parseCharacterClass() {
    const std::size_t start = here();
    advance();  // '['

    auto characterClass = std::make_unique<CharacterClass>(start);
    characterClass->negated = eat('^');

    while (!atEnd() && peek() != ']') {
        ClassItem first;
        if (!parseClassAtom(first)) return nullptr;

        if (first.kind == ClassItem::Kind::Range && peek() == '-') {
            const std::size_t dash = here();
            advance();
            if (atEnd() || peek() == ']') {
                characterClass->items.push_back(first);
                ClassItem literal;
                literal.kind = ClassItem::Kind::Range;
                literal.offset = dash;
                literal.from = '-';
                literal.to = '-';
                characterClass->items.push_back(literal);
                continue;
            }

            ClassItem second;
            if (!parseClassAtom(second)) return nullptr;
            if (second.kind != ClassItem::Kind::Range) {
                // Annex B：ClassEscape 作为范围端点时按并集处理
                // （[\d-a] → \d ∪ '-' ∪ 'a'）。
                if (!(flags_.unicode || flags_.unicodeSets) &&
                    dialect_.annexB) {
                    characterClass->items.push_back(first);
                    ClassItem dashLiteral;
                    dashLiteral.kind = ClassItem::Kind::Range;
                    dashLiteral.offset = dash;
                    dashLiteral.from = '-';
                    dashLiteral.to = '-';
                    characterClass->items.push_back(dashLiteral);
                    characterClass->items.push_back(second);
                    continue;
                }
                fail(dash, "Invalid character class range");
                return nullptr;
            }
            if (second.from < first.from) {
                fail(dash, "Range out of order in character class");
                return nullptr;
            }
            ClassItem range;
            range.kind = ClassItem::Kind::Range;
            range.offset = first.offset;
            range.from = first.from;
            range.to = second.from;
            characterClass->items.push_back(range);
            continue;
        }

        if (first.kind != ClassItem::Kind::Range &&
            (flags_.unicode || flags_.unicodeSets) && peek() == '-') {
            fail(here(), "Invalid character class");
            return nullptr;
        }
        characterClass->items.push_back(first);
    }

    if (!eat(']')) {
        fail(start, "Unterminated character class");
        return nullptr;
    }
    return characterClass;
}

namespace {

bool isVClassReserved(std::uint32_t cp) {
    switch (cp) {
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case '/':
        case '-':
        case '|':
            return true;
        default:
            return false;
    }
}

}  // namespace

NodePtr RegexParser::parseVCharacterClass() {
    const std::size_t start = here();
    advance();  // '['
    auto node = std::make_unique<CharacterClass>(start);
    node->negated = eat('^');

    std::shared_ptr<CharacterClass> current = parseVClassUnion();
    if (!current) return nullptr;

    while (!atEnd() && peek() != ']') {
        CharacterClass::SetOp op = CharacterClass::SetOp::Union;
        if (peek() == '&' && peekNext() == '&') {
            advance();
            advance();
            op = CharacterClass::SetOp::Intersection;
        } else if (peek() == '-' && peekNext() == '-') {
            advance();
            advance();
            op = CharacterClass::SetOp::Subtraction;
        } else {
            fail(here(), "Invalid character class");
            return nullptr;
        }
        std::shared_ptr<CharacterClass> right = parseVClassUnion();
        if (!right) return nullptr;
        auto combined = std::make_shared<CharacterClass>(start);
        combined->setOp = op;
        combined->left = std::move(current);
        combined->right = std::move(right);
        current = std::move(combined);
    }

    if (!eat(']')) {
        fail(start, "Unterminated character class");
        return nullptr;
    }

    if (current->left) {
        node->setOp = current->setOp;
        node->left = std::move(current->left);
        node->right = std::move(current->right);
    } else {
        node->items = std::move(current->items);
    }
    return node;
}

std::shared_ptr<CharacterClass> RegexParser::parseVClassUnion() {
    auto leaf = std::make_shared<CharacterClass>(here());
    bool any = false;
    while (!atEnd() && peek() != ']') {
        if ((peek() == '&' && peekNext() == '&') ||
            (peek() == '-' && peekNext() == '-')) {
            break;
        }

        ClassItem item;
        bool single = false;
        std::uint32_t codePoint = 0;
        if (!parseVClassAtom(item, single, codePoint)) return nullptr;

        if (single && peek() == '-' && peekNext() != '-' && peekNext() != ']') {
            const std::size_t dash = here();
            advance();
            ClassItem second;
            bool secondSingle = false;
            std::uint32_t secondCodePoint = 0;
            if (!parseVClassAtom(second, secondSingle, secondCodePoint)) {
                return nullptr;
            }
            if (!secondSingle) {
                fail(dash, "Invalid character class range");
                return nullptr;
            }
            if (secondCodePoint < codePoint) {
                fail(dash, "Range out of order in character class");
                return nullptr;
            }
            ClassItem range;
            range.kind = ClassItem::Kind::Range;
            range.offset = item.offset;
            range.from = codePoint;
            range.to = secondCodePoint;
            leaf->items.push_back(range);
        } else {
            leaf->items.push_back(std::move(item));
        }
        any = true;
    }
    if (!any) {
        fail(here(), "Invalid character class");
        return nullptr;
    }
    return leaf;
}

bool RegexParser::parseVClassAtom(ClassItem& item, bool& singleCodePoint,
                                  std::uint32_t& codePoint) {
    if (atEnd()) {
        fail(here(), "Unterminated character class");
        return false;
    }
    if (peek() == '[') {
        NodePtr nested = parseVCharacterClass();
        if (!nested) return false;
        item.kind = ClassItem::Kind::Nested;
        item.offset = nested->offset;
        item.nested = std::shared_ptr<CharacterClass>(
            static_cast<CharacterClass*>(nested.release()));
        singleCodePoint = false;
        return true;
    }
    if (peek() == '\\') {
        return parseVClassEscape(item, singleCodePoint, codePoint);
    }
    if (isVClassReserved(peek())) {
        fail(here(), "Invalid character class");
        return false;
    }
    const std::size_t start = here();
    const std::uint32_t cp = advance();
    item.kind = ClassItem::Kind::Range;
    item.offset = start;
    item.from = cp;
    item.to = cp;
    singleCodePoint = true;
    codePoint = cp;
    return true;
}

bool RegexParser::parseVClassEscape(ClassItem& item, bool& singleCodePoint,
                                    std::uint32_t& codePoint) {
    const std::size_t start = here();
    advance();  // '\'
    if (atEnd()) {
        fail(start, "\\ at end of pattern");
        return false;
    }

    const std::uint32_t cp = peek();
    switch (cp) {
        case 'd':
        case 'D':
        case 's':
        case 'S':
        case 'w':
        case 'W':
            item.kind = ClassItem::Kind::Escape;
            item.offset = start;
            switch (cp) {
                case 'd': item.escape = ClassItem::EscapeKind::Digit; break;
                case 'D': item.escape = ClassItem::EscapeKind::NotDigit; break;
                case 's': item.escape = ClassItem::EscapeKind::Space; break;
                case 'S': item.escape = ClassItem::EscapeKind::NotSpace; break;
                case 'w': item.escape = ClassItem::EscapeKind::Word; break;
                default: item.escape = ClassItem::EscapeKind::NotWord; break;
            }
            advance();
            singleCodePoint = false;
            return true;
        case 'p':
        case 'P': {
            const bool negated = cp == 'P';
            advance();
            if (!eat('{')) {
                fail(start, "Invalid property escape");
                return false;
            }
            std::string name;
            if (!parsePropertyName(name)) return false;
            item.kind = ClassItem::Kind::Property;
            item.offset = start;
            item.negated = negated;
            item.property = std::move(name);
            singleCodePoint = false;
            return true;
        }
        case 'q': {
            advance();
            if (!eat('{')) {
                fail(start, "Invalid \\q escape");
                return false;
            }
            auto leaf = std::make_shared<CharacterClass>(start);
            std::vector<std::uint32_t> current;
            bool any = false;
            while (!atEnd() && peek() != '}') {
                if (peek() == '|') {
                    if (current.size() != 1) {
                        fail(start, "properties of strings not implemented");
                        return false;
                    }
                    ClassItem alt;
                    alt.kind = ClassItem::Kind::Range;
                    alt.offset = start;
                    alt.from = current[0];
                    alt.to = current[0];
                    leaf->items.push_back(alt);
                    current.clear();
                    advance();
                    any = true;
                    continue;
                }
                if (peek() == '\\') {
                    fail(start, "Invalid \\q escape");
                    return false;
                }
                current.push_back(advance());
            }
            if (!eat('}')) {
                fail(start, "Unterminated \\q escape");
                return false;
            }
            if (current.size() != 1) {
                fail(start, "properties of strings not implemented");
                return false;
            }
            ClassItem alt;
            alt.kind = ClassItem::Kind::Range;
            alt.offset = start;
            alt.from = current[0];
            alt.to = current[0];
            leaf->items.push_back(alt);
            (void)any;
            item.kind = ClassItem::Kind::Nested;
            item.offset = start;
            item.nested = std::move(leaf);
            singleCodePoint = false;
            return true;
        }
        case 'b':
            advance();
            item.kind = ClassItem::Kind::Range;
            item.offset = start;
            item.from = 0x08;
            item.to = 0x08;
            singleCodePoint = true;
            codePoint = 0x08;
            return true;
        case '0':
            advance();
            if (!atEnd() && isAsciiDigit(peek())) {
                fail(start, "Invalid decimal escape");
                return false;
            }
            item.kind = ClassItem::Kind::Range;
            item.offset = start;
            item.from = 0;
            item.to = 0;
            singleCodePoint = true;
            codePoint = 0;
            return true;
        case 'x': {
            advance();
            std::uint32_t value = 0;
            if (!parseHex(2, value)) {
                fail(start, "Invalid escape");
                return false;
            }
            item.kind = ClassItem::Kind::Range;
            item.offset = start;
            item.from = value;
            item.to = value;
            singleCodePoint = true;
            codePoint = value;
            return true;
        }
        case 'u': {
            advance();
            std::uint32_t value = 0;
            bool ok = false;
            if (eat('{')) {
                const std::size_t begin = here();
                std::uint32_t codePointValue = 0;
                std::uint32_t digits = 0;
                while (!atEnd() && peek() != '}') {
                    const int digit = hexValue(peek());
                    if (digit < 0 || codePointValue > 0x10FFFFu / 16u) {
                        fail(begin, "Invalid Unicode escape");
                        return false;
                    }
                    codePointValue = codePointValue * 16u +
                                     static_cast<std::uint32_t>(digit);
                    if (codePointValue > 0x10FFFFu) {
                        fail(begin, "Invalid Unicode escape");
                        return false;
                    }
                    advance();
                    ++digits;
                }
                if (digits == 0 || !eat('}')) {
                    fail(begin, "Invalid Unicode escape");
                    return false;
                }
                value = codePointValue;
                ok = true;
            } else {
                ok = parseHex(4, value);
            }
            if (!ok) {
                fail(start, "Invalid escape");
                return false;
            }
            item.kind = ClassItem::Kind::Range;
            item.offset = start;
            item.from = value;
            item.to = value;
            singleCodePoint = true;
            codePoint = value;
            return true;
        }
        case 'c': {
            advance();
            if (atEnd() || !isAsciiLetter(peek())) {
                fail(start, "Invalid control escape");
                return false;
            }
            const std::uint32_t letter = advance();
            item.kind = ClassItem::Kind::Range;
            item.offset = start;
            item.from = letter % 32;
            item.to = letter % 32;
            singleCodePoint = true;
            codePoint = letter % 32;
            return true;
        }
        default:
            break;
    }

    if (cp >= '1' && cp <= '9') {
        fail(start, "Invalid class escape");
        return false;
    }
    if (!isSyntaxCharacter(cp) && !isVClassReserved(cp) && cp != '^' &&
        cp != '$' && cp != '.' && cp != '*' && cp != '+' && cp != '?' &&
        cp != '\\') {
        fail(start, "Invalid escape");
        return false;
    }
    advance();
    item.kind = ClassItem::Kind::Range;
    item.offset = start;
    item.from = cp;
    item.to = cp;
    singleCodePoint = true;
    codePoint = cp;
    return true;
}

bool RegexParser::parseClassAtom(ClassItem& item) {
    if (peek() == '\\') return parseClassEscape(item);
    const std::size_t start = here();
    const std::uint32_t cp = advance();
    item.kind = ClassItem::Kind::Range;
    item.offset = start;
    item.from = cp;
    item.to = cp;
    return true;
}

bool RegexParser::parseClassEscape(ClassItem& item) {
    const std::size_t start = here();
    advance();  // '\'
    if (atEnd()) {
        fail(start, "\\ at end of pattern");
        return false;
    }

    const std::uint32_t cp = peek();
    switch (cp) {
        case 'd':
        case 'D':
        case 's':
        case 'S':
        case 'w':
        case 'W': {
            item.kind = ClassItem::Kind::Escape;
            item.offset = start;
            switch (cp) {
                case 'd': item.escape = ClassItem::EscapeKind::Digit; break;
                case 'D': item.escape = ClassItem::EscapeKind::NotDigit; break;
                case 's': item.escape = ClassItem::EscapeKind::Space; break;
                case 'S': item.escape = ClassItem::EscapeKind::NotSpace; break;
                case 'w': item.escape = ClassItem::EscapeKind::Word; break;
                default: item.escape = ClassItem::EscapeKind::NotWord; break;
            }
            advance();
            return true;
        }
        case 'p':
        case 'P': {
            const bool negated = cp == 'P';
            advance();
            if (!(flags_.unicode || flags_.unicodeSets)) {
                if (!dialect_.annexB) {
                    fail(start, "Invalid property escape");
                    return false;
                }
                item.kind = ClassItem::Kind::Range;
                item.offset = start;
                item.from = cp;
                item.to = cp;
                return true;
            }
            if (!eat('{')) {
                fail(start, "Invalid property escape");
                return false;
            }
            std::string name;
            if (!parsePropertyName(name)) return false;
            item.kind = ClassItem::Kind::Property;
            item.offset = start;
            item.negated = negated;
            item.property = std::move(name);
            return true;
        }
        case 'b':
            advance();
            item.kind = ClassItem::Kind::Range;
            item.offset = start;
            item.from = 0x08;
            item.to = 0x08;
            return true;
        case 'c': {
            advance();
            const bool valid = !atEnd() &&
                               (isAsciiLetter(peek()) || isAsciiDigit(peek()) ||
                                peek() == '_');
            if (!valid) {
                // Annex B：ClassControlLetter 非法时，'\' 作为字面量，
                // 'c' 留给后续原子解析。
                if (!(flags_.unicode || flags_.unicodeSets) &&
                    dialect_.annexB) {
                    --pos_;
                    item.kind = ClassItem::Kind::Range;
                    item.offset = start;
                    item.from = '\\';
                    item.to = '\\';
                    return true;
                }
                fail(start, "Invalid control escape");
                return false;
            }
            const std::uint32_t letter = advance();
            item.kind = ClassItem::Kind::Range;
            item.offset = start;
            item.from = letter % 32;
            item.to = letter % 32;
            return true;
        }
        case 'x': {
            advance();
            std::uint32_t value = 0;
            if (!parseHex(2, value)) {
                if (flags_.unicode || flags_.unicodeSets) {
                    fail(start, "Invalid escape");
                    return false;
                }
                if (!dialect_.annexB) {
                    fail(start, "Invalid escape");
                    return false;
                }
                value = 'x';
            }
            item.kind = ClassItem::Kind::Range;
            item.offset = start;
            item.from = value;
            item.to = value;
            return true;
        }
        case 'u': {
            advance();
            std::uint32_t value = 0;
            bool ok = false;
            if ((flags_.unicode || flags_.unicodeSets) && eat('{')) {
                const std::size_t begin = here();
                std::uint32_t codePoint = 0;
                std::uint32_t digits = 0;
                while (!atEnd() && peek() != '}') {
                    const int digit = hexValue(peek());
                    if (digit < 0 || codePoint > 0x10FFFFu / 16u) {
                        fail(begin, "Invalid Unicode escape");
                        return false;
                    }
                    codePoint = codePoint * 16u +
                                static_cast<std::uint32_t>(digit);
                    if (codePoint > 0x10FFFFu) {
                        fail(begin, "Invalid Unicode escape");
                        return false;
                    }
                    advance();
                    ++digits;
                }
                if (digits == 0 || !eat('}')) {
                    fail(begin, "Invalid Unicode escape");
                    return false;
                }
                value = codePoint;
                ok = true;
            } else {
                ok = parseHex(4, value);
            }
            if (!ok) {
                if (flags_.unicode || flags_.unicodeSets) {
                    fail(start, "Invalid escape");
                    return false;
                }
                if (!dialect_.annexB) {
                    fail(start, "Invalid escape");
                    return false;
                }
                value = 'u';
            }
            item.kind = ClassItem::Kind::Range;
            item.offset = start;
            item.from = value;
            item.to = value;
            return true;
        }
        case '0': {
            advance();
            if (!atEnd() && isAsciiDigit(peek())) {
                if (flags_.unicode || flags_.unicodeSets) {
                    fail(start, "Invalid decimal escape");
                    return false;
                }
                if (!dialect_.annexB) {
                    fail(start, "Invalid decimal escape");
                    return false;
                }
                std::uint32_t value = 0;
                std::uint32_t count = 0;
                while (count < 3 && !atEnd()) {
                    const std::uint32_t digit = peek();
                    if (digit < '0' || digit > '7') break;
                    value = value * 8u + (digit - '0');
                    advance();
                    ++count;
                }
                item.kind = ClassItem::Kind::Range;
                item.offset = start;
                item.from = value;
                item.to = value;
                return true;
            }
            item.kind = ClassItem::Kind::Range;
            item.offset = start;
            item.from = 0;
            item.to = 0;
            return true;
        }
        default:
            break;
    }

    if (cp >= '1' && cp <= '9') {
        if (flags_.unicode || flags_.unicodeSets) {
            fail(start, "Invalid class escape");
            return false;
        }
        if (!dialect_.annexB) {
            fail(start, "Invalid class escape");
            return false;
        }
        if (cp <= '7') {
            std::uint32_t value = 0;
            std::uint32_t count = 0;
            while (count < 3 && !atEnd()) {
                const std::uint32_t digit = peek();
                if (digit < '0' || digit > '7') break;
                value = value * 8u + (digit - '0');
                advance();
                ++count;
            }
            item.kind = ClassItem::Kind::Range;
            item.offset = start;
            item.from = value;
            item.to = value;
            return true;
        }
        advance();
        item.kind = ClassItem::Kind::Range;
        item.offset = start;
        item.from = cp;
        item.to = cp;
        return true;
    }

    if ((flags_.unicode || flags_.unicodeSets) && !isSyntaxCharacter(cp) &&
        cp != '-' && cp != '/') {
        fail(start, "Invalid escape");
        return false;
    }
    advance();
    item.kind = ClassItem::Kind::Range;
    item.offset = start;
    item.from = cp;
    item.to = cp;
    return true;
}

NodePtr RegexParser::parseAtomEscape() {
    const std::size_t start = here();
    advance();  // '\'
    if (atEnd()) {
        fail(start, "\\ at end of pattern");
        return nullptr;
    }

    const std::uint32_t cp = peek();
    switch (cp) {
        case 'd':
        case 'D':
        case 's':
        case 'S':
        case 'w':
        case 'W': {
            auto characterClass = std::make_unique<CharacterClass>(start);
            ClassItem item;
            item.kind = ClassItem::Kind::Escape;
            item.offset = start;
            switch (cp) {
                case 'd': item.escape = ClassItem::EscapeKind::Digit; break;
                case 'D': item.escape = ClassItem::EscapeKind::NotDigit; break;
                case 's': item.escape = ClassItem::EscapeKind::Space; break;
                case 'S': item.escape = ClassItem::EscapeKind::NotSpace; break;
                case 'w': item.escape = ClassItem::EscapeKind::Word; break;
                default: item.escape = ClassItem::EscapeKind::NotWord; break;
            }
            characterClass->items.push_back(item);
            advance();
            return characterClass;
        }
        case 'p':
        case 'P': {
            const bool negated = cp == 'P';
            advance();
            if (!(flags_.unicode || flags_.unicodeSets)) {
                if (!dialect_.annexB) {
                    fail(start, "Invalid property escape");
                    return nullptr;
                }
                return std::make_unique<Character>(start, cp);
            }
            if (!eat('{')) {
                fail(start, "Invalid property escape");
                return nullptr;
            }
            std::string name;
            if (!parsePropertyName(name)) return nullptr;
            return std::make_unique<PropertyEscape>(start, negated,
                                                    std::move(name));
        }
        case 'k': {
            advance();
            if (!eat('<')) {
                if (flags_.unicode || flags_.unicodeSets) {
                    fail(start, "Invalid named reference");
                    return nullptr;
                }
                if (!dialect_.annexB) {
                    fail(start, "Invalid named reference");
                    return nullptr;
                }
                return std::make_unique<Character>(start, 'k');
            }
            if (!dialect_.allowBackrefs) {
                fail(start, "Backreferences are disabled");
                return nullptr;
            }
            std::string name;
            if (!parseGroupName(name)) return nullptr;
            auto node =
                std::make_unique<Backreference>(start, 0, name);
            backrefs_.push_back(BackrefRef{start, 0, name, node.get()});
            return node;
        }
        case 'b':
        case 'B': {
            if (flags_.unicode || flags_.unicodeSets) {
                fail(start, "Invalid escape");
                return nullptr;
            }
            if (!dialect_.annexB) {
                fail(start, "Invalid escape");
                return nullptr;
            }
            advance();
            return std::make_unique<Character>(start, cp);
        }
        case 'c': {
            advance();
            if (atEnd() || !isAsciiLetter(peek())) {
                fail(start, "Invalid control escape");
                return nullptr;
            }
            const std::uint32_t letter = advance();
            return std::make_unique<Character>(start, letter % 32);
        }
        case 'x': {
            advance();
            std::uint32_t value = 0;
            if (parseHex(2, value)) {
                return std::make_unique<Character>(start, value);
            }
            if (flags_.unicode || flags_.unicodeSets) {
                fail(start, "Invalid escape");
                return nullptr;
            }
            if (!dialect_.annexB) {
                fail(start, "Invalid escape");
                return nullptr;
            }
            return std::make_unique<Character>(start, 'x');
        }
        case 'u': {
            advance();
            if ((flags_.unicode || flags_.unicodeSets) && eat('{')) {
                const std::size_t begin = here();
                std::uint32_t codePoint = 0;
                std::uint32_t digits = 0;
                while (!atEnd() && peek() != '}') {
                    const int digit = hexValue(peek());
                    if (digit < 0 || codePoint > 0x10FFFFu / 16u) {
                        fail(begin, "Invalid Unicode escape");
                        return nullptr;
                    }
                    codePoint = codePoint * 16u +
                                static_cast<std::uint32_t>(digit);
                    if (codePoint > 0x10FFFFu) {
                        fail(begin, "Invalid Unicode escape");
                        return nullptr;
                    }
                    advance();
                    ++digits;
                }
                if (digits == 0 || !eat('}')) {
                    fail(begin, "Invalid Unicode escape");
                    return nullptr;
                }
                return std::make_unique<Character>(start, codePoint);
            }
            std::uint32_t value = 0;
            if (parseHex(4, value)) {
                return std::make_unique<Character>(start, value);
            }
            if (flags_.unicode || flags_.unicodeSets) {
                fail(start, "Invalid escape");
                return nullptr;
            }
            if (!dialect_.annexB) {
                fail(start, "Invalid escape");
                return nullptr;
            }
            return std::make_unique<Character>(start, 'u');
        }
        case '0': {
            advance();
            if (!atEnd() && isAsciiDigit(peek())) {
                if (flags_.unicode || flags_.unicodeSets) {
                    fail(start, "Invalid decimal escape");
                    return nullptr;
                }
                if (!dialect_.annexB) {
                    fail(start, "Invalid decimal escape");
                    return nullptr;
                }
                std::uint32_t value = 0;
                std::uint32_t count = 0;
                while (count < 3 && !atEnd()) {
                    const std::uint32_t digit = peek();
                    if (digit < '0' || digit > '7') break;
                    value = value * 8u + (digit - '0');
                    advance();
                    ++count;
                }
                return std::make_unique<Character>(start, value);
            }
            return std::make_unique<Character>(start, 0);
        }
        default:
            break;
    }

    if (cp >= '1' && cp <= '9') {
        // Annex B：非 unicode 模式下 \8 / \9 是身份转义（字面量 8/9）。
        if ((cp == '8' || cp == '9') && !(flags_.unicode || flags_.unicodeSets) &&
            dialect_.annexB) {
            advance();
            return std::make_unique<Character>(start, cp);
        }
        if (!dialect_.allowBackrefs) {
            fail(start, "Backreferences are disabled");
            return nullptr;
        }
        std::uint64_t index = 0;
        std::string digits;
        while (!atEnd() && isAsciiDigit(peek())) {
            index = index * 10u + static_cast<std::uint64_t>(peek() - '0');
            if (index > std::numeric_limits<std::uint32_t>::max()) {
                fail(start, "Backreference index too large");
                return nullptr;
            }
            digits.push_back(static_cast<char>(peek()));
            advance();
        }
        auto node = std::make_unique<Backreference>(
            start, static_cast<std::uint32_t>(index));
        node->digits = std::move(digits);
        backrefs_.push_back(BackrefRef{start,
                                       static_cast<std::uint32_t>(index), {},
                                       node.get()});
        return node;
    }

    if ((flags_.unicode || flags_.unicodeSets) && !isSyntaxCharacter(cp) &&
        cp != '/') {
        fail(start, "Invalid escape");
        return nullptr;
    }
    advance();
    return std::make_unique<Character>(start, cp);
}

bool RegexParser::parsePropertyName(std::string& name) {
    const std::size_t start = here();
    while (!atEnd() && peek() != '}') {
        const std::uint32_t cp = peek();
        if (cp == '{' || cp == '\\') {
            fail(here(), "Invalid property name");
            return false;
        }
        unicode::encodeCodePoint(cp, name);
        advance();
    }
    if (name.empty()) {
        fail(start, "Invalid property name");
        return false;
    }
    if (!eat('}')) {
        fail(start, "Unterminated property name");
        return false;
    }
    return true;
}

bool RegexParser::parseGroupName(std::string& name) {
    if (atEnd()) {
        fail(here(), "Invalid capture group name");
        return false;
    }
    if (!unicode::isIdentifierStart(peek())) {
        fail(here(), "Invalid capture group name");
        return false;
    }
    while (!atEnd() && peek() != '>') {
        if (!unicode::isIdentifierContinue(peek())) {
            fail(here(), "Invalid capture group name");
            return false;
        }
        unicode::encodeCodePoint(peek(), name);
        advance();
    }
    if (!eat('>')) {
        fail(here(), "Unterminated group name");
        return false;
    }
    return true;
}

bool RegexParser::parseDecimal(std::uint32_t& out) {
    const std::size_t start = here();
    std::uint64_t value = 0;
    bool any = false;
    while (!atEnd() && isAsciiDigit(peek())) {
        any = true;
        value = value * 10u + static_cast<std::uint64_t>(peek() - '0');
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            fail(start, "Quantifier too large");
            return false;
        }
        advance();
    }
    if (!any) return false;
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool RegexParser::parseHex(std::uint32_t digits, std::uint32_t& out) {
    const std::size_t start = here();
    std::uint32_t value = 0;
    for (std::uint32_t i = 0; i < digits; ++i) {
        if (atEnd()) {
            pos_ = start;
            return false;
        }
        const int digit = hexValue(peek());
        if (digit < 0) {
            pos_ = start;
            return false;
        }
        value = value * 16u + static_cast<std::uint32_t>(digit);
        advance();
    }
    out = value;
    return true;
}

RegexParser::QuantResult RegexParser::tryParseQuantifierSpec(
    QuantSpec& spec) {
    const std::size_t start = here();
    spec = QuantSpec{};
    spec.offset = start;

    if (atEnd()) return QuantResult::None;
    const std::uint32_t cp = peek();
    if (cp == '*') {
        advance();
        spec.min = 0;
        spec.max = std::numeric_limits<std::uint32_t>::max();
    } else if (cp == '+') {
        advance();
        spec.min = 1;
        spec.max = std::numeric_limits<std::uint32_t>::max();
    } else if (cp == '?') {
        advance();
        spec.min = 0;
        spec.max = 1;
    } else if (cp == '{') {
        advance();
        std::uint32_t min = 0;
        if (!parseDecimal(min)) {
            if (failed_) return QuantResult::Error;
            pos_ = start;
            return QuantResult::None;
        }
        std::uint32_t max = min;
        if (eat(',')) {
            if (peek() == '}') {
                max = std::numeric_limits<std::uint32_t>::max();
            } else if (!parseDecimal(max)) {
                if (failed_) return QuantResult::Error;
                pos_ = start;
                return QuantResult::None;
            }
        }
        if (!eat('}')) {
            pos_ = start;
            return QuantResult::None;
        }
        if (max != std::numeric_limits<std::uint32_t>::max() && max < min) {
            fail(start, "Numbers out of order in {} quantifier");
            return QuantResult::Error;
        }
        spec.min = min;
        spec.max = max;
    } else {
        return QuantResult::None;
    }

    spec.greedy = !eat('?');
    return QuantResult::Spec;
}

void RegexParser::validateBackrefs() {
    if (failed_) return;
    for (const BackrefRef& ref : backrefs_) {
        if (!ref.name.empty()) {
            if (namedGroups_.find(ref.name) == namedGroups_.end()) {
                fail(ref.offset, "Invalid named capture referenced");
                return;
            }
            continue;
        }
        if (ref.index == 0 || ref.index > captureCount_) {
            // Annex B：无效数字反向引用退化为 LegacyOctalEscapeSequence。
            if (!(flags_.unicode || flags_.unicodeSets) && dialect_.annexB &&
                ref.node != nullptr) {
                ref.node->legacyOctal = true;
                continue;
            }
            fail(ref.offset, "Invalid backreference");
            return;
        }
    }
}

void RegexParser::fail(std::size_t offset, std::string message) {
    if (failed_) return;
    failed_ = true;
    error_ = CompileError{offset, std::move(message)};
}

std::uint32_t RegexParser::peek() {
    if (atEnd()) return 0;
    std::size_t index = pos_;
    std::uint32_t codePoint = 0;
    if (unicode::decodeCodePoint(pattern_, index, codePoint)) {
        return codePoint;
    }
    return static_cast<unsigned char>(pattern_[pos_]);
}

std::uint32_t RegexParser::peekNext() {
    const std::size_t saved = pos_;
    advance();
    const std::uint32_t codePoint = peek();
    pos_ = saved;
    return codePoint;
}

std::uint32_t RegexParser::advance() {
    if (atEnd()) return 0;
    std::size_t index = pos_;
    std::uint32_t codePoint = 0;
    if (unicode::decodeCodePoint(pattern_, index, codePoint)) {
        pos_ = index;
        return codePoint;
    }
    ++pos_;
    return static_cast<unsigned char>(pattern_[pos_ - 1]);
}

bool RegexParser::eat(std::uint32_t codePoint) {
    if (atEnd() || peek() != codePoint) return false;
    advance();
    return true;
}

}  // namespace neo::regex
