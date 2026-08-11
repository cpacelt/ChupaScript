#include "parser.hpp"

#include <cstdint>
#include <vector>

#include "lexer.hpp"
#include "token.hpp"

namespace CS {
namespace {

/// Предел глубины рекурсии парсера.
///
/// Вход недоверенный, а без предела тексты вида "((((((…", "!!!!!!…" и
/// "1 ?? 1 ?? 1 ?? …" роняют процесс переполнением стека. Счётчик растёт в
/// трёх самотрекурсивных правилах — ternary, nilCoalesce, unary, — поэтому
/// один уровень вложенности скобок стоит трёх единиц, а цепочка из '!' или
/// '??' — одной за звено.
///
/// 96 единиц — это около 320 кадров стека в худшем случае, то есть меньше
/// сотни килобайт. Двадцати уровней вложенности в макете не бывает.
constexpr std::uint32_t kMaxDepth = 96;

bool isComparisonOp(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::Equal:
        case TokenKind::NotEqual:
        case TokenKind::Less:
        case TokenKind::Greater:
        case TokenKind::LessEqual:
        case TokenKind::GreaterEqual:
            return true;
        default:
            return false;
    }
}

class Parser {
   public:
    Parser(const char *source, std::uint32_t length, Ast &ast)
        : lexer_(source, length), ast_(ast) {}

    /// Стартовый символ Expression, docs/grammar.md §5.1.
    bool runExpression(Diagnostic &diag);

   private:
    /// Считает глубину вложенности; уменьшает её на любом выходе из правила.
    class DepthGuard {
       public:
        explicit DepthGuard(std::uint32_t &depth) noexcept : depth_(depth) {
            ++depth_;
        }
        ~DepthGuard() { --depth_; }
        DepthGuard(const DepthGuard &) = delete;
        DepthGuard &operator=(const DepthGuard &) = delete;

       private:
        std::uint32_t &depth_;
    };

    bool advance();
    [[nodiscard]] bool at(TokenKind kind) const noexcept {
        return cur_.kind == kind;
    }

    /// Записывает первый отказ и возвращает kNoNode.
    ///
    /// Последующие вызовы отказ не переписывают: диагностика лексера,
    /// пришедшая первой, обязана дойти до вызывающего неизменной.
    NodeId fail(std::uint32_t offset, const char *message);

    NodeId ternary();
    NodeId logicalOr();
    NodeId logicalAnd();
    NodeId comparison();
    NodeId nilCoalesce();
    NodeId additive();
    NodeId multiplicative();
    NodeId unary();
    NodeId postfix();
    NodeId primary();

    NodeId callArguments(const Token &name);
    NodeId arrayLiteral();
    NodeId objectLiteral();

    /// Общий буфер списков детей: аргументы вызова, элементы литералов,
    /// стейтменты программы. Правило помечает вершину, толкает свои узлы и
    /// откатывает буфер, забрав их.
    ///
    /// Откат делается только на успешном пути: после отказа разбор
    /// прекращается навсегда, и остатки в буфере никого не касаются.
    std::vector<NodeId> scratch_;   // TODO(B10): вместе с боковым пулом детей

    Lexer lexer_;
    Ast &ast_;
    Token cur_{};
    Diagnostic diag_{};
    bool failed_ = false;
    std::uint32_t depth_ = 0;
};

bool Parser::advance() {
    if (!lexer_.next(cur_, diag_)) {
        failed_ = true;
        return false;
    }
    return true;
}

NodeId Parser::fail(std::uint32_t offset, const char *message) {
    if (!failed_) {
        diag_ = Diagnostic{ErrorCode::Syntax, offset, message};
        failed_ = true;
    }
    return kNoNode;
}

// ─── §5.3, от низшего приоритета к высшему ───────────────────────────

NodeId Parser::ternary() {
    const DepthGuard guard(depth_);
    if (depth_ > kMaxDepth) {
        return fail(cur_.offset, "expression nesting too deep");
    }
    const NodeId condition = logicalOr();
    if (condition == kNoNode) {
        return kNoNode;
    }
    if (!at(TokenKind::Question)) {
        return condition;
    }
    const std::uint32_t offset = cur_.offset;
    if (!advance()) {
        return kNoNode;
    }
    const NodeId whenTrue = ternary();
    if (whenTrue == kNoNode) {
        return kNoNode;
    }
    if (!at(TokenKind::Colon)) {
        return fail(cur_.offset, "expected ':' in conditional expression");
    }
    if (!advance()) {
        return kNoNode;
    }
    const NodeId whenFalse = ternary();
    if (whenFalse == kNoNode) {
        return kNoNode;
    }
    return ast_.conditional(condition, whenTrue, whenFalse, offset);
}

NodeId Parser::logicalOr() {
    NodeId lhs = logicalAnd();
    if (lhs == kNoNode) {
        return kNoNode;
    }
    while (at(TokenKind::OrOr)) {
        const std::uint32_t offset = cur_.offset;
        if (!advance()) {
            return kNoNode;
        }
        const NodeId rhs = logicalAnd();
        if (rhs == kNoNode) {
            return kNoNode;
        }
        lhs = ast_.binary(TokenKind::OrOr, lhs, rhs, offset);
    }
    return lhs;
}

NodeId Parser::logicalAnd() {
    NodeId lhs = comparison();
    if (lhs == kNoNode) {
        return kNoNode;
    }
    while (at(TokenKind::AndAnd)) {
        const std::uint32_t offset = cur_.offset;
        if (!advance()) {
            return kNoNode;
        }
        const NodeId rhs = comparison();
        if (rhs == kNoNode) {
            return kNoNode;
        }
        lhs = ast_.binary(TokenKind::AndAnd, lhs, rhs, offset);
    }
    return lhs;
}

NodeId Parser::comparison() {
    const NodeId lhs = nilCoalesce();
    if (lhs == kNoNode) {
        return kNoNode;
    }
    if (!isComparisonOp(cur_.kind)) {
        return lhs;
    }
    const TokenKind op = cur_.kind;
    const std::uint32_t offset = cur_.offset;
    if (!advance()) {
        return kNoNode;
    }
    const NodeId rhs = nilCoalesce();
    if (rhs == kNoNode) {
        return kNoNode;
    }
    // §5.4: уровень неассоциативен. Второе сравнение подряд — ранняя ошибка
    // §5.5, а не «лишний текст»: сообщение обязано называть причину.
    if (isComparisonOp(cur_.kind)) {
        return fail(cur_.offset, "chained comparison is not allowed");
    }
    return ast_.binary(op, lhs, rhs, offset);
}

NodeId Parser::nilCoalesce() {
    const DepthGuard guard(depth_);
    if (depth_ > kMaxDepth) {
        return fail(cur_.offset, "expression nesting too deep");
    }
    const NodeId lhs = additive();
    if (lhs == kNoNode) {
        return kNoNode;
    }
    if (!at(TokenKind::QuestionQuestion)) {
        return lhs;
    }
    const std::uint32_t offset = cur_.offset;
    if (!advance()) {
        return kNoNode;
    }
    // Правая ассоциативность — рекурсия в себя, а не цикл.
    const NodeId rhs = nilCoalesce();
    if (rhs == kNoNode) {
        return kNoNode;
    }
    return ast_.binary(TokenKind::QuestionQuestion, lhs, rhs, offset);
}

NodeId Parser::additive() {
    NodeId lhs = multiplicative();
    if (lhs == kNoNode) {
        return kNoNode;
    }
    while (at(TokenKind::Plus) || at(TokenKind::Minus)) {
        const TokenKind op = cur_.kind;
        const std::uint32_t offset = cur_.offset;
        if (!advance()) {
            return kNoNode;
        }
        const NodeId rhs = multiplicative();
        if (rhs == kNoNode) {
            return kNoNode;
        }
        lhs = ast_.binary(op, lhs, rhs, offset);
    }
    return lhs;
}

NodeId Parser::multiplicative() {
    NodeId lhs = unary();
    if (lhs == kNoNode) {
        return kNoNode;
    }
    while (at(TokenKind::Star) || at(TokenKind::Slash) ||
           at(TokenKind::Percent)) {
        const TokenKind op = cur_.kind;
        const std::uint32_t offset = cur_.offset;
        if (!advance()) {
            return kNoNode;
        }
        const NodeId rhs = unary();
        if (rhs == kNoNode) {
            return kNoNode;
        }
        lhs = ast_.binary(op, lhs, rhs, offset);
    }
    return lhs;
}

NodeId Parser::unary() {
    const DepthGuard guard(depth_);
    if (depth_ > kMaxDepth) {
        return fail(cur_.offset, "expression nesting too deep");
    }
    if (at(TokenKind::Bang) || at(TokenKind::Minus)) {
        const TokenKind op = cur_.kind;
        const std::uint32_t offset = cur_.offset;
        if (!advance()) {
            return kNoNode;
        }
        const NodeId operand = unary();
        if (operand == kNoNode) {
            return kNoNode;
        }
        return ast_.unary(op, operand, offset);
    }
    return postfix();
}

NodeId Parser::postfix() {
    NodeId base = primary();
    if (base == kNoNode) {
        return kNoNode;
    }
    for (;;) {
        if (at(TokenKind::Dot)) {
            if (!advance()) {
                return kNoNode;
            }
            if (!at(TokenKind::Identifier)) {
                return fail(cur_.offset, "expected a field name after '.'");
            }
            const Token name = cur_;
            if (!advance()) {
                return kNoNode;
            }
            base = ast_.member(base, name);
        } else if (at(TokenKind::LBracket)) {
            const std::uint32_t offset = cur_.offset;
            if (!advance()) {
                return kNoNode;
            }
            const NodeId subscript = ternary();
            if (subscript == kNoNode) {
                return kNoNode;
            }
            if (!at(TokenKind::RBracket)) {
                return fail(cur_.offset, "expected ']'");
            }
            if (!advance()) {
                return kNoNode;
            }
            base = ast_.index(base, subscript, offset);
        } else {
            return base;
        }
    }
}

NodeId Parser::callArguments(const Token &name) {
    // Вход: cur_ — открывающая скобка вызова.
    if (!advance()) {
        return kNoNode;
    }
    const std::size_t mark = scratch_.size();
    if (!at(TokenKind::RParen)) {
        for (;;) {
            const NodeId arg = ternary();
            if (arg == kNoNode) {
                return kNoNode;
            }
            scratch_.push_back(arg);
            if (!at(TokenKind::Comma)) {
                break;
            }
            if (!advance()) {
                return kNoNode;
            }
            if (at(TokenKind::RParen)) {
                return fail(cur_.offset, "trailing comma");
            }
        }
    }
    if (!at(TokenKind::RParen)) {
        return fail(cur_.offset, "expected ')'");
    }
    if (!advance()) {
        return kNoNode;
    }
    const auto count = static_cast<std::uint32_t>(scratch_.size() - mark);
    const NodeId node = ast_.call(name, scratch_.data() + mark, count);
    scratch_.resize(mark);
    return node;
}

NodeId Parser::arrayLiteral() {
    const std::uint32_t offset = cur_.offset;  // '['
    if (!advance()) {
        return kNoNode;
    }
    const std::size_t mark = scratch_.size();
    if (!at(TokenKind::RBracket)) {
        for (;;) {
            const NodeId item = ternary();
            if (item == kNoNode) {
                return kNoNode;
            }
            scratch_.push_back(item);
            if (!at(TokenKind::Comma)) {
                break;
            }
            if (!advance()) {
                return kNoNode;
            }
            if (at(TokenKind::RBracket)) {
                return fail(cur_.offset, "trailing comma");
            }
        }
    }
    if (!at(TokenKind::RBracket)) {
        return fail(cur_.offset, "expected ']'");
    }
    if (!advance()) {
        return kNoNode;
    }
    const auto count = static_cast<std::uint32_t>(scratch_.size() - mark);
    const NodeId node = ast_.array(scratch_.data() + mark, count, offset);
    scratch_.resize(mark);
    return node;
}

NodeId Parser::objectLiteral() {
    const std::uint32_t offset = cur_.offset;  // '{'
    if (!advance()) {
        return kNoNode;
    }
    const std::size_t mark = scratch_.size();
    if (!at(TokenKind::RBrace)) {
        for (;;) {
            if (!at(TokenKind::String)) {
                return fail(cur_.offset,
                            "object key must be a string literal");
            }
            const Token key = cur_;
            if (!advance()) {
                return kNoNode;
            }
            if (!at(TokenKind::Colon)) {
                return fail(cur_.offset, "expected ':' after object key");
            }
            if (!advance()) {
                return kNoNode;
            }
            // Ключ кладётся до разбора значения: вложенный литерал пометит
            // вершину буфера уже за ним и вернёт её на место сам.
            scratch_.push_back(ast_.string(key));
            const NodeId value = ternary();
            if (value == kNoNode) {
                return kNoNode;
            }
            scratch_.push_back(value);
            if (!at(TokenKind::Comma)) {
                break;
            }
            if (!advance()) {
                return kNoNode;
            }
            if (at(TokenKind::RBrace)) {
                return fail(cur_.offset, "trailing comma");
            }
        }
    }
    if (!at(TokenKind::RBrace)) {
        return fail(cur_.offset, "expected '}'");
    }
    if (!advance()) {
        return kNoNode;
    }
    const auto count = static_cast<std::uint32_t>(scratch_.size() - mark);
    const NodeId node = ast_.object(scratch_.data() + mark, count, offset);
    scratch_.resize(mark);
    return node;
}

NodeId Parser::primary() {
    switch (cur_.kind) {
        case TokenKind::Number: {
            const Token token = cur_;
            if (!advance()) {
                return kNoNode;
            }
            return ast_.number(token);
        }
        case TokenKind::String: {
            const Token token = cur_;
            if (!advance()) {
                return kNoNode;
            }
            return ast_.string(token);
        }
        case TokenKind::True:
        case TokenKind::False: {
            const Token token = cur_;
            if (!advance()) {
                return kNoNode;
            }
            return ast_.boolean(token);
        }
        case TokenKind::Null: {
            const Token token = cur_;
            if (!advance()) {
                return kNoNode;
            }
            return ast_.null(token);
        }
        case TokenKind::Identifier: {
            const Token token = cur_;
            if (!advance()) {
                return kNoNode;
            }
            // §5.3: Call — альтернатива Primary, а не постфиксная операция.
            // Поэтому f(a)(b) не разбирается, а keys(o)[0] — да.
            if (at(TokenKind::LParen)) {
                return callArguments(token);
            }
            return ast_.identifier(token);
        }
        case TokenKind::LParen: {
            if (!advance()) {
                return kNoNode;
            }
            // Скобки узла не порождают: группировка уже выражена формой дерева.
            const NodeId inner = ternary();
            if (inner == kNoNode) {
                return kNoNode;
            }
            if (!at(TokenKind::RParen)) {
                return fail(cur_.offset, "expected ')'");
            }
            if (!advance()) {
                return kNoNode;
            }
            return inner;
        }
        case TokenKind::LBracket:
            return arrayLiteral();
        case TokenKind::LBrace:
            return objectLiteral();
        default:
            return fail(cur_.offset, "expected an expression");
    }
}

bool Parser::runExpression(Diagnostic &diag) {
    if (!advance()) {
        diag = diag_;
        return false;
    }
    const NodeId root = ternary();
    if (root == kNoNode) {
        diag = diag_;
        return false;
    }
    if (at(TokenKind::Semicolon)) {
        fail(cur_.offset, "';' is not allowed in expression mode");
        diag = diag_;
        return false;
    }
    if (!at(TokenKind::End)) {
        fail(cur_.offset, "trailing text after expression");
        diag = diag_;
        return false;
    }
    ast_.setRoot(root);
    return true;
}

}  // namespace

bool parseExpression(const char *source, std::uint32_t length, Ast &ast,
                     Diagnostic &diag) {
    ast.setSource(source);
    Parser parser(source, length, ast);
    return parser.runExpression(diag);
}

bool parseProgram(const char *source, std::uint32_t length, Ast &ast,
                  Diagnostic &diag) {
    ast.setSource(source);
    static_cast<void>(length);
    diag = Diagnostic{ErrorCode::Syntax, 0, "program parsing not implemented"};
    return false;
}

}  // namespace CS
