#include "data.hpp"

#include <cassert>
#include <cstdint>
#include <string>

#include "ast.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "token.hpp"

namespace CS {
namespace {

/// Записывает отказ «в данных выражение недопустимо» с местом узла.
bool rejectNode(const Ast &ast, NodeId node, Diagnostic &diag) {
    diag = Diagnostic{ErrorCode::Data, ast.offset(node),
                      "expression is not allowed in data"};
    return false;
}

/// Идентификатор ли это по docs/grammar.md §4.4 и не ключевое ли слово (§4.5).
///
/// Проверка выполняется лексером, а не своей таблицей: так набор ключевых слов
/// и ограничение на ASCII заведомо совпадают с языком и не разъедутся с ним.
bool isRootName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 0xffffffffu) { return false; }

    Lexer lexer(name.data(), static_cast<std::uint32_t>(name.size()));
    Diagnostic ignored;

    Token first;
    if (!lexer.next(first, ignored)) { return false; }
    if (first.kind != TokenKind::Identifier) { return false; }
    // Токен обязан покрывать имя целиком: иначе " state" и "state " прошли бы,
    // а обратиться к такому корню нельзя.
    if (first.offset != 0 || first.length != name.size()) { return false; }

    Token tail;
    if (!lexer.next(tail, ignored)) { return false; }
    return tail.kind == TokenKind::End;
}

/// Раскодирует экранирование строкового литерала.
///
/// Набор — из docs/grammar.md §A: \\ \' \" \n \t. Неизвестной
/// последовательности здесь быть не может: её отверг лексер.
void decodeEscapes(std::string_view raw, std::string &out) {
    out.clear();
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\') {
            out.push_back(raw[i]);
            continue;
        }
        assert(i + 1 < raw.size() && "лексер не пропустил бы висячий слэш");
        ++i;
        switch (raw[i]) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case '\\': out.push_back('\\'); break;
            case '\'': out.push_back('\''); break;
            case '"': out.push_back('"'); break;
            default: assert(false && "лексер отверг бы такую последовательность");
        }
    }
}

/// Содержимое строкового литерала: срез исходника, если экранирования нет,
/// иначе раскодированное в scratch.
///
/// Флаг hasEscape избавляет от временного буфера в подавляющем большинстве
/// случаев: экранирование в данных редкость.
std::string_view literalText(const Ast &ast, NodeId node, std::string &scratch) {
    assert(ast.kind(node) == NodeKind::String);
    if (!ast.hasEscape(node)) { return ast.text(node); }
    decodeEscapes(ast.text(node), scratch);
    return scratch;
}

bool materialize(const Ast &ast, NodeId node, Context &ctx,
                 Value *out, Diagnostic &diag) {
    switch (ast.kind(node)) {
        case NodeKind::Number:
            *out = Value::number(ast.numberValue(node));
            return true;

        case NodeKind::Boolean:
            *out = Value::boolean(ast.boolValue(node));
            return true;

        case NodeKind::Null:
            *out = Value::null();
            return true;

        case NodeKind::String: {
            std::string scratch;
            *out = ctx.makeString(literalText(ast, node, scratch));
            return true;
        }

        case NodeKind::Unary: {
            // Минус над числом — это запись отрицательного значения, а не
            // вычисление: знака в NumericLiteral нет (docs/grammar.md §4.6),
            // и без этой ветки первое же отрицательное поле с бэкенда упёрлось
            // бы в «выражения в данных запрещены».
            if (ast.op(node) != TokenKind::Minus) { return rejectNode(ast, node, diag); }
            const NodeId operand = ast.child(node, 0);
            if (ast.kind(operand) != NodeKind::Number) {
                return rejectNode(ast, node, diag);
            }
            *out = Value::number(-ast.numberValue(operand));
            return true;
        }

        case NodeKind::Array: {
            const std::uint32_t count = ast.childCount(node);
            // Размер известен заранее, поэтому ёмкость выделяется точно и
            // построение не оставляет мусора.
            const Value array = ctx.makeArray(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                Value element = Value::null();
                if (!materialize(ast, ast.child(node, i), ctx, &element, diag)) {
                    return false;
                }
                ctx.arrayPush(array, element);
            }
            *out = array;
            return true;
        }

        case NodeKind::Object: {
            // Дети чередуются: ключ, значение, ключ, значение.
            const std::uint32_t count = ast.childCount(node);
            const Value object = ctx.makeObject(count / 2);
            std::string scratch;
            for (std::uint32_t i = 0; i + 1 < count; i += 2) {
                Value value = Value::null();
                if (!materialize(ast, ast.child(node, i + 1), ctx, &value, diag)) {
                    return false;
                }
                // Повторный ключ заменяет значение: последний выигрывает.
                ctx.objectSet(object, literalText(ast, ast.child(node, i), scratch),
                              value);
            }
            *out = object;
            return true;
        }

        default:
            return rejectNode(ast, node, diag);
    }
}

}  // namespace

bool setVariable(Context &ctx, std::string_view name, std::string_view text,
                 Diagnostic &diag) {
    if (!isRootName(name)) {
        diag = Diagnostic{ErrorCode::Name, 0, "root name must be an identifier"};
        return false;
    }

    // Та же защита, что в isRootName: длина обязана влезать в std::uint32_t,
    // иначе приведение ниже молча усечёт text до префикса, а этот префикс
    // способен успешно разобраться как совсем другое значение.
    if (text.size() > 0xffffffffu) {
        diag = Diagnostic{ErrorCode::Data, 0, "value text is too long"};
        return false;
    }

    Ast ast;
    if (!parseExpression(text.data(), static_cast<std::uint32_t>(text.size()), ast,
                         diag)) {
        return false;
    }

    Value value = Value::null();
    if (!materialize(ast, ast.root(), ctx, &value, diag)) { return false; }

    // Корень заводится только после успеха: отказ не оставляет имени.
    ctx.setRoot(name, value);
    return true;
}

}  // namespace CS
