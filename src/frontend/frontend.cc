#include "neojs/frontend/frontend.h"

#include <sstream>

#include "neojs/frontend/lexer.h"
#include "neojs/frontend/parser.h"

namespace neojs {

namespace {

const char *tokenKindName(TokenKind k) {
  switch (k) {
    case TokenKind::EndOfFile:
      return "eof";
    case TokenKind::Number:
      return "number";
    case TokenKind::String:
      return "string";
    case TokenKind::RegExp:
      return "regexp";
    case TokenKind::TemplateStart:
      return "templatestart";
    case TokenKind::TemplatePart:
      return "templatepart";
    case TokenKind::TemplateEnd:
      return "templateend";
    case TokenKind::Identifier:
      return "identifier";
    case TokenKind::Symbol:
      return "symbol";
    case TokenKind::Error:
      return "error";
  }
  return "?";
}

const char *unopName(UnaryOpKind k) {
  switch (k) {
    case UnaryOpKind::Pos:
      return "+";
    case UnaryOpKind::Neg:
      return "-";
    case UnaryOpKind::Not:
      return "!";
    case UnaryOpKind::BitNot:
      return "~";
    case UnaryOpKind::Typeof:
      return "typeof";
    case UnaryOpKind::Void:
      return "void";
    case UnaryOpKind::Delete:
      return "delete";
    case UnaryOpKind::Await:
      return "await";
  }
  return "?";
}

const char *binopName(BinaryOpKind k) {
  switch (k) {
    case BinaryOpKind::Add:
      return "+";
    case BinaryOpKind::Sub:
      return "-";
    case BinaryOpKind::Mul:
      return "*";
    case BinaryOpKind::Div:
      return "/";
    case BinaryOpKind::Mod:
      return "%";
    case BinaryOpKind::Pow:
      return "**";
    case BinaryOpKind::LShift:
      return "<<";
    case BinaryOpKind::RShift:
      return ">>";
    case BinaryOpKind::URShift:
      return ">>>";
    case BinaryOpKind::Lt:
      return "<";
    case BinaryOpKind::Le:
      return "<=";
    case BinaryOpKind::Gt:
      return ">";
    case BinaryOpKind::Ge:
      return ">=";
    case BinaryOpKind::In:
      return "in";
    case BinaryOpKind::Instanceof:
      return "instanceof";
    case BinaryOpKind::Eq:
      return "==";
    case BinaryOpKind::Ne:
      return "!=";
    case BinaryOpKind::StrictEq:
      return "===";
    case BinaryOpKind::StrictNe:
      return "!==";
    case BinaryOpKind::BitAnd:
      return "&";
    case BinaryOpKind::BitXor:
      return "^";
    case BinaryOpKind::BitOr:
      return "|";
    case BinaryOpKind::LogicalAnd:
      return "&&";
    case BinaryOpKind::LogicalOr:
      return "||";
    case BinaryOpKind::Nullish:
      return "??";
  }
  return "?";
}

void dumpNode(const ASTNode *node, std::string &out, int depth) {
  if (node == nullptr) return;
  out.append(depth * 2, ' ');
  switch (node->type) {
    case NodeType::Program:
      out += "Program\n";
      break;
    case NodeType::ExprStmt:
      out += "ExprStmt\n";
      break;
    case NodeType::Literal:
      if (node->literal.is_number) {
        out += "Literal(";
        out += node->literal.text; /* 保留原始文本 */
        out += ")\n";
      } else if (node->literal.is_string) {
        out += "Literal(\"";
        out += node->literal.str;
        out += "\")\n";
      } else if (node->literal.is_boolean) {
        out += node->literal.bool_value ? "Literal(true)\n" : "Literal(false)\n";
      } else if (node->literal.is_null) {
        out += "Literal(null)\n";
      } else {
        out += "Literal\n";
      }
      break;
    case NodeType::Identifier:
      out += "Identifier(";
      out += node->name;
      out += ")\n";
      break;
    case NodeType::PrivateIdentifier:
      out += "PrivateIdentifier(";
      out += node->name;
      out += ")\n";
      break;
    case NodeType::RegExpLiteral:
      out += "RegExpLiteral(/";
      out += node->regexp.pattern;
      out += "/";
      out += node->regexp.flags;
      out += ")\n";
      break;
    case NodeType::TemplateLiteral:
      out += "TemplateLiteral\n";
      break;
    case NodeType::ObjectPattern:
      out += "ObjectPattern\n";
      break;
    case NodeType::ArrayPattern:
      out += "ArrayPattern\n";
      break;
    case NodeType::AssignmentPattern:
      out += "AssignmentPattern\n";
      break;
    case NodeType::RestElement:
      out += "RestElement\n";
      break;
    case NodeType::Property:
      out += "Property\n";
      break;
    case NodeType::SpreadElement:
      out += "SpreadElement\n";
      break;
    case NodeType::UnaryOp:
      out += "UnaryOp(";
      out += unopName(node->unop);
      out += ")\n";
      break;
    case NodeType::BinaryOp:
      out += "BinaryOp(";
      out += binopName(node->binop);
      out += ")\n";
      break;
  }
  for (const ASTNode *child : node->children) dumpNode(child, out, depth + 1);
}

}  // namespace

AST *parseSource(JSAllocator *allocator, const char *source, size_t source_len,
                 DiagnosticSink &sink) {
  Parser parser(allocator, source, source_len, sink);
  return parser.parse();
}

std::string dumpTokens(const char *source, size_t source_len, DiagnosticSink &sink) {
  std::string out;
  Lexer lexer(source, source_len, sink);
  /* 协同模式下 Lexer 由 Parser 驱动；dump-tokens 独立驱动整个流 */
  for (;;) {
    const Token &t = lexer.next();
    out += tokenKindName(t.kind);
    out += " '";
    out += t.text;
    out += "' @";
    out += std::to_string(t.start.line);
    out += ":";
    out += std::to_string(t.start.col);
    out += "\n";
    if (t.kind == TokenKind::EndOfFile) break;
  }
  return out;
}

std::string dumpAst(const AST &ast) {
  std::string out;
  dumpNode(ast.root, out, 0);
  return out;
}

}  // namespace neojs
