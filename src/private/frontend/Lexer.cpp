// frontend/Lexer.cpp - 拉取式词法器实现
//
// 扫描顺序：trivia（空白/注释/行终止符）→ 按 LexicalGoal 与首码点分派。
// 所有扫描函数失败时把原因写入 error_，由 next() 统一转成 Illegal token；
// 失败路径保证至少前进一个码点（除非已在输入末尾），避免调用方死循环。

#include "frontend/Lexer.h"

#include "bigint/BigInt.h"
#include "unicode/UnicodeClassification.h"
#include "unicode/Utf8.h"

#include <charconv>
#include <cstdlib>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace neo::frontend {

namespace {

bool isDecimalDigit(uint32_t cp) { return cp >= '0' && cp <= '9'; }

int digitValue(uint32_t cp) {
    if (cp >= '0' && cp <= '9') return static_cast<int>(cp - '0');
    if (cp >= 'a' && cp <= 'f') return static_cast<int>(cp - 'a') + 10;
    if (cp >= 'A' && cp <= 'F') return static_cast<int>(cp - 'A') + 10;
    return -1;
}

bool isDigitForRadix(uint32_t cp, int radix) {
    const int value = digitValue(cp);
    return value >= 0 && value < radix;
}

TokenKind keywordKind(std::string_view name) {
    struct Entry {
        std::string_view name;
        TokenKind kind;
    };
    static constexpr Entry kKeywords[] = {
        {"break", TokenKind::Break},       {"case", TokenKind::Case},
        {"catch", TokenKind::Catch},       {"class", TokenKind::Class},
        {"const", TokenKind::Const},       {"continue", TokenKind::Continue},
        {"debugger", TokenKind::Debugger}, {"default", TokenKind::Default},
        {"delete", TokenKind::Delete},     {"do", TokenKind::Do},
        {"else", TokenKind::Else},         {"export", TokenKind::Export},
        {"extends", TokenKind::Extends},   {"finally", TokenKind::Finally},
        {"for", TokenKind::For},           {"function", TokenKind::Function},
        {"if", TokenKind::If},             {"import", TokenKind::Import},
        {"in", TokenKind::In},             {"instanceof", TokenKind::InstanceOf},
        {"new", TokenKind::New},           {"return", TokenKind::Return},
        {"super", TokenKind::Super},       {"switch", TokenKind::Switch},
        {"this", TokenKind::This},         {"throw", TokenKind::Throw},
        {"try", TokenKind::Try},           {"typeof", TokenKind::TypeOf},
        {"var", TokenKind::Var},           {"void", TokenKind::Void},
        {"while", TokenKind::While},       {"with", TokenKind::With},
        {"enum", TokenKind::Enum},         {"null", TokenKind::Null},
        {"true", TokenKind::True},         {"false", TokenKind::False},
    };
    for (const Entry& entry : kKeywords) {
        if (entry.name == name) return entry.kind;
    }
    return TokenKind::Identifier;
}

}  // namespace

Token Lexer::next(const LexicalContext& context) {
    error_.clear();

    SourceLocation triviaStart = stream_.location();
    bool sawLineTerminator = false;
    std::string triviaError;
    if (!skipTrivia(sawLineTerminator, triviaStart, triviaError)) {
        return makeIllegal(triviaStart, std::move(triviaError));
    }

    Token token;
    token.start = stream_.location();
    token.precededByLineTerminator = sawLineTerminator;

    if (stream_.atEnd()) {
        token.kind = TokenKind::EndOfInput;
        finish(token);
        return token;
    }

    const uint32_t cp = stream_.peek();
    bool ok = true;

    if (context.goal == LexicalGoal::TemplateContinuation) {
        if (cp == '}') {
            stream_.advance();
            ok = scanTemplate(token, false, context);
        } else {
            stream_.advance();
            error_ = "Expected '}' to continue template literal";
            ok = false;
        }
    } else if (context.goal == LexicalGoal::RegExp && cp == '/') {
        ok = scanRegExp(token);
    } else if (cp == '\\' || unicode::isIdentifierStart(cp)) {
        ok = scanIdentifier(token, true);
    } else if (cp == '#') {
        ok = scanPrivateIdentifier(token);
    } else if (isDecimalDigit(cp) ||
               (cp == '.' && isDecimalDigit(stream_.peek(1)))) {
        ok = scanNumber(token, context);
    } else if (cp == '"' || cp == '\'') {
        ok = scanString(token, context);
    } else if (cp == '`') {
        stream_.advance();
        ok = scanTemplate(token, true, context);
    } else {
        ok = scanPunctuator(token, context);
    }

    if (!ok) {
        return makeIllegal(token.start, error_.empty() ? "Invalid token" : error_);
    }
    finish(token);
    return token;
}

void Lexer::finish(Token& token) {
    token.end = stream_.location();
    const size_t length = token.end.offset - token.start.offset;
    token.lexeme = stream_.source().substr(token.start.offset, length);
}

Token Lexer::makeIllegal(SourceLocation start, std::string message) {
    Token token;
    token.kind = TokenKind::Illegal;
    token.start = start;
    token.text = std::move(message);
    // 保证失败路径至少消费一个码点，避免调用方在同一位置反复取到 Illegal。
    if (!stream_.atEnd() && stream_.offset() == start.offset) stream_.advance();
    finish(token);
    return token;
}

bool Lexer::skipTrivia(bool& sawLineTerminator, SourceLocation& errorStart,
                       std::string& error) {
    while (!stream_.atEnd()) {
        const uint32_t cp = stream_.peek();

        // 文件开头的 hashbang。
        if (stream_.offset() == 0 && cp == '#' && stream_.peek(1) == '!') {
            while (!stream_.atEnd() &&
                   !unicode::isLineTerminator(stream_.peek())) {
                stream_.advance();
            }
            continue;
        }

        if (unicode::isWhitespace(cp)) {
            stream_.advance();
            continue;
        }
        if (unicode::isLineTerminator(cp)) {
            sawLineTerminator = true;
            stream_.advance();
            continue;
        }

        if (cp == '/' && stream_.peek(1) == '/') {
            stream_.advance();
            stream_.advance();
            while (!stream_.atEnd() &&
                   !unicode::isLineTerminator(stream_.peek())) {
                stream_.advance();
            }
            continue;
        }

        if (cp == '/' && stream_.peek(1) == '*') {
            errorStart = stream_.location();
            stream_.advance();
            stream_.advance();
            bool closed = false;
            while (!stream_.atEnd()) {
                const uint32_t inner = stream_.peek();
                if (inner == '*' && stream_.peek(1) == '/') {
                    stream_.advance();
                    stream_.advance();
                    closed = true;
                    break;
                }
                if (unicode::isLineTerminator(inner)) sawLineTerminator = true;
                stream_.advance();
            }
            if (!closed) {
                error = "Unterminated comment";
                return false;
            }
            continue;
        }

        break;
    }
    return true;
}

bool Lexer::scanIdentifier(Token& token, bool allowEscape) {
    token.text.clear();
    bool first = true;

    while (true) {
        const uint32_t cp = stream_.peek();
        if (cp == '\\' && allowEscape) {
            uint32_t escaped = 0;
            if (!scanUnicodeEscape(escaped)) return false;
            const bool valid = first ? unicode::isIdentifierStart(escaped)
                                     : unicode::isIdentifierContinue(escaped);
            if (!valid) {
                error_ = "Invalid Unicode escape in identifier";
                return false;
            }
            unicode::encodeCodePoint(escaped, token.text);
        } else if (first ? unicode::isIdentifierStart(cp)
                         : unicode::isIdentifierContinue(cp)) {
            unicode::encodeCodePoint(cp, token.text);
            stream_.advance();
        } else {
            break;
        }
        first = false;
    }

    if (token.text.empty()) {
        error_ = "Expected identifier";
        return false;
    }
    token.kind = keywordKind(token.text);
    return true;
}

bool Lexer::scanPrivateIdentifier(Token& token) {
    stream_.advance();  // '#'
    if (!scanIdentifier(token, true)) return false;
    token.kind = TokenKind::PrivateIdentifier;
    return true;
}

bool Lexer::scanDigits(int radix, std::string& digits, bool requireOne) {
    bool sawDigit = false;
    bool lastWasSeparator = false;

    while (true) {
        const uint32_t cp = stream_.peek();
        if (isDigitForRadix(cp, radix)) {
            digits.push_back(static_cast<char>(cp));
            sawDigit = true;
            lastWasSeparator = false;
            stream_.advance();
        } else if (cp == '_') {
            if (!sawDigit || lastWasSeparator) {
                error_ = "Invalid numeric separator";
                return false;
            }
            lastWasSeparator = true;
            stream_.advance();
        } else {
            break;
        }
    }

    if (lastWasSeparator) {
        error_ = "Invalid numeric separator";
        return false;
    }
    if (requireOne && !sawDigit) {
        error_ = "Expected digits";
        return false;
    }
    return true;
}

bool Lexer::scanNumber(Token& token, const LexicalContext& context) {
    const uint32_t first = stream_.peek();

    // 0x / 0o / 0b
    if (first == '0') {
        const uint32_t second = stream_.peek(1);
        int radix = 0;
        if (second == 'x' || second == 'X') radix = 16;
        else if (second == 'o' || second == 'O') radix = 8;
        else if (second == 'b' || second == 'B') radix = 2;

        if (radix != 0) {
            stream_.advance();
            stream_.advance();
            std::string digits;
            if (!scanDigits(radix, digits)) return false;

            if (stream_.peek() == 'n') {
                stream_.advance();
                token.kind = TokenKind::BigIntLiteral;
                token.text = std::move(digits);
                token.numericRadix = radix;
                return true;
            }

            bigint::BigInt value;
            if (!bigint::BigInt::parse(digits, radix, value)) {
                error_ = "Invalid numeric literal";
                return false;
            }
            token.kind = TokenKind::NumericLiteral;
            token.numberValue = value.toDouble();
            return true;
        }
    }

    // 十进制 / legacy octal
    std::string intDigits;
    bool sawDigit = false;
    bool lastWasSeparator = false;
    bool hadSeparator = false;

    while (true) {
        const uint32_t cp = stream_.peek();
        if (isDecimalDigit(cp)) {
            intDigits.push_back(static_cast<char>(cp));
            sawDigit = true;
            lastWasSeparator = false;
            stream_.advance();
        } else if (cp == '_') {
            if (!sawDigit || lastWasSeparator) {
                error_ = "Invalid numeric separator";
                return false;
            }
            lastWasSeparator = true;
            hadSeparator = true;
            stream_.advance();
        } else {
            break;
        }
    }

    if (!sawDigit && first != '.') {
        error_ = "Expected digits";
        return false;
    }
    if (lastWasSeparator) {
        error_ = "Invalid numeric separator";
        return false;
    }

    const bool legacyLeadingZero = intDigits.size() > 1 && intDigits[0] == '0';
    const uint32_t afterInt = stream_.peek();

    if (legacyLeadingZero) {
        if (afterInt == 'n') {
            error_ = "Invalid BigInt literal";
            return false;
        }
        if (afterInt == '.' || afterInt == 'e' || afterInt == 'E') {
            error_ = "Invalid numeric literal";
            return false;
        }
        if (hadSeparator) {
            error_ = "Invalid numeric separator";
            return false;
        }
        if (context.strict) {
            error_ = "Legacy octal literals are not allowed in strict mode";
            return false;
        }

        bool octal = true;
        for (const char digit : intDigits) {
            if (digit < '0' || digit > '7') {
                octal = false;
                break;
            }
        }

        bigint::BigInt value;
        if (!bigint::BigInt::parse(intDigits, octal ? 8 : 10, value)) {
            error_ = "Invalid numeric literal";
            return false;
        }
        token.kind = TokenKind::NumericLiteral;
        token.numberValue = value.toDouble();
        return true;
    }

    std::string number = intDigits;

    if (afterInt == '.') {
        stream_.advance();
        number.push_back('.');
        std::string fraction;
        if (!scanDigits(10, fraction, false)) return false;
        number += fraction;
    }

    const uint32_t afterFraction = stream_.peek();
    if (afterFraction == 'e' || afterFraction == 'E') {
        stream_.advance();
        number.push_back('e');
        const uint32_t sign = stream_.peek();
        if (sign == '+' || sign == '-') {
            number.push_back(static_cast<char>(sign));
            stream_.advance();
        }
        std::string exponent;
        if (!scanDigits(10, exponent)) {
            error_ = "Expected exponent digits";
            return false;
        }
        number += exponent;
    }

    if (stream_.peek() == 'n') {
        if (number.find('.') != std::string::npos ||
            number.find('e') != std::string::npos) {
            error_ = "Invalid BigInt literal";
            return false;
        }
        stream_.advance();
        token.kind = TokenKind::BigIntLiteral;
        token.text = std::move(intDigits);
        token.numericRadix = 10;
        return true;
    }

    double value = 0.0;
    const auto result =
        std::from_chars(number.data(), number.data() + number.size(), value);
    if (result.ec == std::errc::result_out_of_range) {
        value = std::strtod(number.c_str(), nullptr);
    } else if (result.ec != std::errc{}) {
        error_ = "Invalid numeric literal";
        return false;
    }

    token.kind = TokenKind::NumericLiteral;
    token.numberValue = value;
    return true;
}

bool Lexer::scanString(Token& token, const LexicalContext& context) {
    const uint32_t quote = stream_.advance();
    token.text.clear();

    while (true) {
        if (stream_.atEnd()) {
            error_ = "Unterminated string literal";
            return false;
        }
        const uint32_t cp = stream_.peek();
        if (cp == quote) {
            stream_.advance();
            break;
        }
        if (unicode::isLineTerminator(cp)) {
            error_ = "Unterminated string literal";
            return false;
        }
        if (cp == '\\') {
            if (!scanStringEscape(token.text, false, context)) return false;
            continue;
        }
        unicode::encodeCodePoint(cp, token.text);
        stream_.advance();
    }

    token.kind = TokenKind::StringLiteral;
    return true;
}

bool Lexer::scanTemplate(Token& token, bool head,
                         const LexicalContext& context) {
    token.text.clear();

    while (true) {
        if (stream_.atEnd()) {
            error_ = "Unterminated template literal";
            return false;
        }
        const uint32_t cp = stream_.peek();

        if (cp == '`') {
            stream_.advance();
            token.kind =
                head ? TokenKind::NoSubstitutionTemplate : TokenKind::TemplateTail;
            return true;
        }
        if (cp == '$' && stream_.peek(1) == '{') {
            stream_.advance();
            stream_.advance();
            token.kind =
                head ? TokenKind::TemplateHead : TokenKind::TemplateMiddle;
            return true;
        }
        if (cp == '\\') {
            if (!scanStringEscape(token.text, true, context)) return false;
            continue;
        }
        if (cp == '\r') {
            // 模板 cooked 值把 CRLF / CR 归一化为 LF。
            stream_.advance();
            if (stream_.peek() == '\n') stream_.advance();
            token.text.push_back('\n');
            continue;
        }
        unicode::encodeCodePoint(cp, token.text);
        stream_.advance();
    }
}

bool Lexer::scanStringEscape(std::string& out, bool templateMode,
                             const LexicalContext& context) {
    stream_.advance();  // '\\'
    if (stream_.atEnd()) {
        error_ = "Unterminated escape sequence";
        return false;
    }

    uint32_t cp = stream_.peek();
    switch (cp) {
        case 'b':
            out.push_back('\b');
            stream_.advance();
            return true;
        case 'f':
            out.push_back('\f');
            stream_.advance();
            return true;
        case 'n':
            out.push_back('\n');
            stream_.advance();
            return true;
        case 'r':
            out.push_back('\r');
            stream_.advance();
            return true;
        case 't':
            out.push_back('\t');
            stream_.advance();
            return true;
        case 'v':
            out.push_back('\v');
            stream_.advance();
            return true;
        case '0':
            stream_.advance();
            if (isDecimalDigit(stream_.peek())) {
                if (templateMode || context.strict) {
                    error_ = "Octal escape sequences are not allowed here";
                    return false;
                }
                int value = 0;
                int count = 0;
                while (count < 2 && stream_.peek() >= '0' &&
                       stream_.peek() <= '7') {
                    value = value * 8 + static_cast<int>(stream_.peek() - '0');
                    stream_.advance();
                    ++count;
                }
                out.push_back(static_cast<char>(value));
                return true;
            }
            out.push_back('\0');
            return true;
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7': {
            if (templateMode || context.strict) {
                error_ = "Octal escape sequences are not allowed here";
                return false;
            }
            int value = 0;
            int count = 0;
            while (count < 3 && stream_.peek() >= '0' &&
                   stream_.peek() <= '7') {
                value = value * 8 + static_cast<int>(stream_.peek() - '0');
                stream_.advance();
                ++count;
            }
            if (value > 0xFF) {
                error_ = "Octal escape sequence out of range";
                return false;
            }
            out.push_back(static_cast<char>(value));
            return true;
        }
        case '8':
        case '9':
            if (templateMode || context.strict) {
                error_ = "\\8 and \\9 are not allowed here";
                return false;
            }
            out.push_back(static_cast<char>(cp));
            stream_.advance();
            return true;
        case 'x': {
            stream_.advance();
            uint32_t value = 0;
            for (int index = 0; index < 2; ++index) {
                const int digit = digitValue(stream_.peek());
                if (digit < 0) {
                    error_ = "Invalid hexadecimal escape sequence";
                    return false;
                }
                value = value * 16 + static_cast<uint32_t>(digit);
                stream_.advance();
            }
            out.push_back(static_cast<char>(value));
            return true;
        }
        case 'u': {
            stream_.advance();
            uint32_t value = 0;
            if (!scanUnicodeEscapeBody(value)) return false;
            unicode::encodeCodePoint(value, out);
            return true;
        }
        case '\r':
            stream_.advance();
            if (stream_.peek() == '\n') stream_.advance();
            return true;
        case '\n':
        case 0x2028u:
        case 0x2029u:
            stream_.advance();
            return true;
        default:
            break;
    }

    if (unicode::isLineTerminator(cp)) {
        stream_.advance();
        return true;
    }

    // NonEscapeCharacter：反斜杠后的普通字符按原样进入 cooked 值。
    unicode::encodeCodePoint(cp, out);
    stream_.advance();
    return true;
}

bool Lexer::scanUnicodeEscape(uint32_t& cp) {
    if (stream_.peek() != '\\' || stream_.peek(1) != 'u') {
        error_ = "Expected Unicode escape sequence";
        return false;
    }
    stream_.advance();
    stream_.advance();
    return scanUnicodeEscapeBody(cp);
}

bool Lexer::scanUnicodeEscapeBody(uint32_t& cp) {
    uint32_t value = 0;

    if (stream_.peek() == '{') {
        stream_.advance();
        int count = 0;
        while (stream_.peek() != '}') {
            const int digit = digitValue(stream_.peek());
            if (digit < 0) {
                error_ = "Invalid Unicode escape sequence";
                return false;
            }
            value = value * 16 + static_cast<uint32_t>(digit);
            if (value > 0x10FFFFu) {
                error_ = "Unicode escape out of range";
                return false;
            }
            stream_.advance();
            ++count;
            if (count > 6) {
                error_ = "Invalid Unicode escape sequence";
                return false;
            }
        }
        if (count == 0) {
            error_ = "Invalid Unicode escape sequence";
            return false;
        }
        stream_.advance();  // '}'
    } else {
        for (int index = 0; index < 4; ++index) {
            const int digit = digitValue(stream_.peek());
            if (digit < 0) {
                error_ = "Invalid Unicode escape sequence";
                return false;
            }
            value = value * 16 + static_cast<uint32_t>(digit);
            stream_.advance();
        }
    }

    cp = value;
    return true;
}

bool Lexer::scanRegExp(Token& token) {
    stream_.advance();  // 开头的 '/'

    std::string pattern;
    bool inClass = false;
    bool closed = false;

    while (!stream_.atEnd()) {
        const uint32_t cp = stream_.peek();
        if (unicode::isLineTerminator(cp)) break;

        if (cp == '\\') {
            pattern.push_back('\\');
            stream_.advance();
            if (stream_.atEnd() || unicode::isLineTerminator(stream_.peek())) {
                error_ = "Unterminated regular expression literal";
                return false;
            }
            unicode::encodeCodePoint(stream_.peek(), pattern);
            stream_.advance();
            continue;
        }

        if (cp == '[') {
            inClass = true;
        } else if (cp == ']') {
            inClass = false;
        } else if (cp == '/' && !inClass) {
            stream_.advance();
            closed = true;
            break;
        }

        unicode::encodeCodePoint(cp, pattern);
        stream_.advance();
    }

    if (!closed) {
        error_ = "Unterminated regular expression literal";
        return false;
    }

    std::string flags;
    while (!stream_.atEnd() && unicode::isIdentifierContinue(stream_.peek())) {
        unicode::encodeCodePoint(stream_.peek(), flags);
        stream_.advance();
    }

    token.kind = TokenKind::RegularExpressionLiteral;
    token.text = std::move(pattern);
    token.regexFlags = std::move(flags);
    return true;
}

bool Lexer::scanPunctuator(Token& token, const LexicalContext&) {
    const uint32_t cp = stream_.peek();
    const auto take = [this](size_t count) {
        while (count-- > 0) stream_.advance();
    };

    switch (cp) {
        case '{':
            take(1);
            token.kind = TokenKind::LeftBrace;
            return true;
        case '}':
            take(1);
            token.kind = TokenKind::RightBrace;
            return true;
        case '(':
            take(1);
            token.kind = TokenKind::LeftParen;
            return true;
        case ')':
            take(1);
            token.kind = TokenKind::RightParen;
            return true;
        case '[':
            take(1);
            token.kind = TokenKind::LeftBracket;
            return true;
        case ']':
            take(1);
            token.kind = TokenKind::RightBracket;
            return true;
        case ';':
            take(1);
            token.kind = TokenKind::Semicolon;
            return true;
        case ',':
            take(1);
            token.kind = TokenKind::Comma;
            return true;
        case ':':
            take(1);
            token.kind = TokenKind::Colon;
            return true;
        case '~':
            take(1);
            token.kind = TokenKind::BitNot;
            return true;
        case '.':
            if (stream_.peek(1) == '.' && stream_.peek(2) == '.') {
                take(3);
                token.kind = TokenKind::Ellipsis;
            } else {
                take(1);
                token.kind = TokenKind::Dot;
            }
            return true;
        case '?':
            if (stream_.peek(1) == '?' && stream_.peek(2) == '=') {
                take(3);
                token.kind = TokenKind::NullishAssign;
            } else if (stream_.peek(1) == '?') {
                take(2);
                token.kind = TokenKind::NullishCoalescing;
            } else if (stream_.peek(1) == '.' &&
                       !isDecimalDigit(stream_.peek(2))) {
                take(2);
                token.kind = TokenKind::QuestionDot;
            } else {
                take(1);
                token.kind = TokenKind::Question;
            }
            return true;
        case '=':
            if (stream_.peek(1) == '=' && stream_.peek(2) == '=') {
                take(3);
                token.kind = TokenKind::StrictEqual;
            } else if (stream_.peek(1) == '=') {
                take(2);
                token.kind = TokenKind::EqualEqual;
            } else if (stream_.peek(1) == '>') {
                take(2);
                token.kind = TokenKind::Arrow;
            } else {
                take(1);
                token.kind = TokenKind::Assign;
            }
            return true;
        case '!':
            if (stream_.peek(1) == '=' && stream_.peek(2) == '=') {
                take(3);
                token.kind = TokenKind::StrictNotEqual;
            } else if (stream_.peek(1) == '=') {
                take(2);
                token.kind = TokenKind::NotEqual;
            } else {
                take(1);
                token.kind = TokenKind::LogicalNot;
            }
            return true;
        case '<':
            if (stream_.peek(1) == '<' && stream_.peek(2) == '=') {
                take(3);
                token.kind = TokenKind::LeftShiftAssign;
            } else if (stream_.peek(1) == '<') {
                take(2);
                token.kind = TokenKind::LeftShift;
            } else if (stream_.peek(1) == '=') {
                take(2);
                token.kind = TokenKind::LessThanEqual;
            } else {
                take(1);
                token.kind = TokenKind::LessThan;
            }
            return true;
        case '>':
            if (stream_.peek(1) == '>' && stream_.peek(2) == '>' &&
                stream_.peek(3) == '=') {
                take(4);
                token.kind = TokenKind::UnsignedRightShiftAssign;
            } else if (stream_.peek(1) == '>' && stream_.peek(2) == '>') {
                take(3);
                token.kind = TokenKind::UnsignedRightShift;
            } else if (stream_.peek(1) == '>' && stream_.peek(2) == '=') {
                take(3);
                token.kind = TokenKind::RightShiftAssign;
            } else if (stream_.peek(1) == '>') {
                take(2);
                token.kind = TokenKind::RightShift;
            } else if (stream_.peek(1) == '=') {
                take(2);
                token.kind = TokenKind::GreaterThanEqual;
            } else {
                take(1);
                token.kind = TokenKind::GreaterThan;
            }
            return true;
        case '+':
            if (stream_.peek(1) == '+') {
                take(2);
                token.kind = TokenKind::PlusPlus;
            } else if (stream_.peek(1) == '=') {
                take(2);
                token.kind = TokenKind::PlusAssign;
            } else {
                take(1);
                token.kind = TokenKind::Plus;
            }
            return true;
        case '-':
            if (stream_.peek(1) == '-') {
                take(2);
                token.kind = TokenKind::MinusMinus;
            } else if (stream_.peek(1) == '=') {
                take(2);
                token.kind = TokenKind::MinusAssign;
            } else {
                take(1);
                token.kind = TokenKind::Minus;
            }
            return true;
        case '*':
            if (stream_.peek(1) == '*' && stream_.peek(2) == '=') {
                take(3);
                token.kind = TokenKind::StarStarAssign;
            } else if (stream_.peek(1) == '*') {
                take(2);
                token.kind = TokenKind::StarStar;
            } else if (stream_.peek(1) == '=') {
                take(2);
                token.kind = TokenKind::StarAssign;
            } else {
                take(1);
                token.kind = TokenKind::Star;
            }
            return true;
        case '/':
            if (stream_.peek(1) == '=') {
                take(2);
                token.kind = TokenKind::SlashAssign;
            } else {
                take(1);
                token.kind = TokenKind::Slash;
            }
            return true;
        case '%':
            if (stream_.peek(1) == '=') {
                take(2);
                token.kind = TokenKind::PercentAssign;
            } else {
                take(1);
                token.kind = TokenKind::Percent;
            }
            return true;
        case '&':
            if (stream_.peek(1) == '&' && stream_.peek(2) == '=') {
                take(3);
                token.kind = TokenKind::LogicalAndAssign;
            } else if (stream_.peek(1) == '&') {
                take(2);
                token.kind = TokenKind::LogicalAnd;
            } else if (stream_.peek(1) == '=') {
                take(2);
                token.kind = TokenKind::BitAndAssign;
            } else {
                take(1);
                token.kind = TokenKind::BitAnd;
            }
            return true;
        case '|':
            if (stream_.peek(1) == '|' && stream_.peek(2) == '=') {
                take(3);
                token.kind = TokenKind::LogicalOrAssign;
            } else if (stream_.peek(1) == '|') {
                take(2);
                token.kind = TokenKind::LogicalOr;
            } else if (stream_.peek(1) == '=') {
                take(2);
                token.kind = TokenKind::BitOrAssign;
            } else {
                take(1);
                token.kind = TokenKind::BitOr;
            }
            return true;
        case '^':
            if (stream_.peek(1) == '=') {
                take(2);
                token.kind = TokenKind::BitXorAssign;
            } else {
                take(1);
                token.kind = TokenKind::BitXor;
            }
            return true;
        default:
            break;
    }

    error_ = "Unexpected character";
    stream_.advance();
    return false;
}

}  // namespace neo::frontend
