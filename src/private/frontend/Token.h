// frontend/Token.h - 词法单元
//
// Token 由 Lexer 按需产出，不预生成 token 流（正则/除法的 goal 依赖 parser 上下文）。
// lexeme 是源码切片（string_view），生命周期与源缓冲一致；text 是解码后的值：
//   - Identifier / PrivateIdentifier：解码后的名字（含 \u 转义）
//   - StringLiteral / Template*：cooked 字符串（UTF-8 / WTF-8）
//   - RegularExpressionLiteral：pattern（不含两侧 /）
//   - BigIntLiteral：去掉前缀/下划线/n 后的数字串
//
// 关联 ADR: 052（前端源流）、054（词法器）

#ifndef NEO_FRONTEND_TOKEN_H
#define NEO_FRONTEND_TOKEN_H

#include "frontend/CharStream.h"

#include <string>
#include <string_view>

namespace neo::frontend {

enum class TokenKind {
    EndOfInput,
    Illegal,

    Identifier,
    PrivateIdentifier,

    NumericLiteral,
    BigIntLiteral,
    StringLiteral,
    RegularExpressionLiteral,
    NoSubstitutionTemplate,
    TemplateHead,
    TemplateMiddle,
    TemplateTail,

    // 固定保留字（上下文关键字如 let/async/await/yield 仍产出 Identifier）
    Break,
    Case,
    Catch,
    Class,
    Const,
    Continue,
    Debugger,
    Default,
    Delete,
    Do,
    Else,
    Export,
    Extends,
    Finally,
    For,
    Function,
    If,
    Import,
    In,
    InstanceOf,
    New,
    Return,
    Super,
    Switch,
    This,
    Throw,
    Try,
    TypeOf,
    Var,
    Void,
    While,
    With,
    Enum,
    Null,
    True,
    False,

    // 标点/运算符
    LeftBrace,
    RightBrace,
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    Dot,
    Ellipsis,
    Semicolon,
    Comma,
    Question,
    QuestionDot,
    Colon,
    Arrow,

    LessThan,
    GreaterThan,
    LessThanEqual,
    GreaterThanEqual,
    EqualEqual,
    NotEqual,
    StrictEqual,
    StrictNotEqual,

    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    StarStar,
    PlusPlus,
    MinusMinus,

    LeftShift,
    RightShift,
    UnsignedRightShift,

    BitAnd,
    BitOr,
    BitXor,
    BitNot,
    LogicalNot,
    LogicalAnd,
    LogicalOr,
    NullishCoalescing,

    Assign,
    PlusAssign,
    MinusAssign,
    StarAssign,
    SlashAssign,
    PercentAssign,
    StarStarAssign,
    LeftShiftAssign,
    RightShiftAssign,
    UnsignedRightShiftAssign,
    BitAndAssign,
    BitOrAssign,
    BitXorAssign,
    LogicalAndAssign,
    LogicalOrAssign,
    NullishAssign,
};

struct Token {
    TokenKind kind = TokenKind::EndOfInput;
    SourceLocation start;
    SourceLocation end;
    // 该 token 之前是否出现过行终止符（ASI / ++/-- 限制需要）。
    bool precededByLineTerminator = false;

    // 源码切片（不含前后 trivia）。
    std::string_view lexeme;

    // 解码后的值，语义见文件头。
    std::string text;

    // NumericLiteral 的值。
    double numberValue = 0.0;
    // BigIntLiteral 的进制与数字串（text 中）。
    int numericRadix = 10;
    // RegularExpressionLiteral 的 flags。
    std::string regexFlags;

    // 以下形态在 sloppy 下合法、在 strict 下是早期错误。Lexer 只记录，
    // 由 Sema 按作用域严格性判定。
    bool legacyLeadingZero = false;   // 010 / 08 / 09
    bool legacyOctalEscape = false;   // \1 ... \7
    bool legacyEightOrNine = false;   // \8 / \9
};

}  // namespace neo::frontend

#endif  // NEO_FRONTEND_TOKEN_H
