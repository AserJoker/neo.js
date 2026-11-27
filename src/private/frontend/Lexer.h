// frontend/Lexer.h - 拉取式词法器
//
// Parser 决定词法目标（正则/除法、模板续段），Lexer 按需扫描一个 Token。
// Lexer 不感知严格模式：sloppy 超集形态（legacy octal / octal escape）
// 照常扫描并在 Token 上打标，由 Sema 按作用域严格性判定。
// 不预生成 token 流：`/` 的正则/除法、模板的 `}` 续段都依赖 parser 上下文。
//
// 用法：
//   Lexer lexer(source);
//   Token t = lexer.next({LexicalGoal::RegExp, false});
//   ...
//   LexerState s = lexer.save();      // 语法层前瞻
//   Token look = lexer.next(...);
//   lexer.restore(s);
//
// 关联 ADR: 052（前端源流）、054（词法器）

#ifndef NEO_FRONTEND_LEXER_H
#define NEO_FRONTEND_LEXER_H

#include "frontend/CharStream.h"
#include "frontend/Token.h"

#include <string>
#include <string_view>

namespace neo::frontend {

// name 是否为保留字（关键字 / 未来保留字 / null/true/false）。
// 含 Unicode 转义的标识符仍按解码后的文本判定保留字。
bool isReservedWord(std::string_view name);

enum class LexicalGoal {
    Div,                  // 表达式位置：/ 是除法
    RegExp,               // 表达式起始：/ 是正则
    TemplateContinuation  // 模板中：当前 } 继续扫模板 chunk
};

struct LexicalContext {
    LexicalGoal goal = LexicalGoal::Div;
};

struct LexerState {
    CharStreamState stream;
    bool atLineStart = true;
};

class Lexer {
public:
    // allowHtmlComments：Script 代码允许 Annex B 的 `<!--` / 行首 `-->`
    // 单行注释；Module 代码不允许。
    explicit Lexer(std::string_view source, bool allowHtmlComments = true)
        : stream_(source), allowHtmlComments_(allowHtmlComments) {}

    Token next(const LexicalContext& context = {});

    const CharStream& stream() const { return stream_; }

    LexerState save() const { return {stream_.save(), atLineStart_}; }
    void restore(const LexerState& state) {
        stream_.restore(state.stream);
        atLineStart_ = state.atLineStart;
    }

private:
    Token makeIllegal(SourceLocation start, std::string message);
    void finish(Token& token);

    bool skipTrivia(bool& sawLineTerminator, SourceLocation& errorStart,
                    std::string& error);

    bool scanIdentifier(Token& token, bool allowEscape);
    bool scanPrivateIdentifier(Token& token);
    bool scanNumber(Token& token);
    bool scanString(Token& token);
    bool scanTemplate(Token& token, bool head);
    bool scanRegExp(Token& token);
    bool scanPunctuator(Token& token, const LexicalContext& context);

    bool scanDigits(int radix, std::string& digits, bool requireOne = true);
    bool scanUnicodeEscape(uint32_t& cp);
    bool scanUnicodeEscapeBody(uint32_t& cp);
    bool scanStringEscape(std::string& out, bool templateMode, Token& token);

    CharStream stream_;
    // 最近一次扫描失败的原因；next() 用它构造 Illegal token。
    std::string error_;
    bool allowHtmlComments_ = true;
    // 当前位置之前自上一个行终止符（或输入起点）起是否只有空白/单行块注释。
    // 行首 `-->` 注释的识别依赖它。
    bool atLineStart_ = true;
};

}  // namespace neo::frontend

#endif  // NEO_FRONTEND_LEXER_H
