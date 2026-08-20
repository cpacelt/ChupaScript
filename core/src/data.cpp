#include "data.hpp"

#include "aggregate.hpp"

#include <cassert>
#include <cstdint>
#include <string>

#include "ast.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "text.hpp"
#include "token.hpp"

namespace CS {
namespace {

/// Записывает отказ «в данных выражение недопустимо» с местом узла.
bool rejectNode(const Ast &ast, NodeId node, Diagnostic &diag) {
    diag = Diagnostic{ErrorCode::Data, ast.offset(node),
                      "expression is not allowed in data"};
    return false;
}

/// Строит значение по узлу литерала. Названа не materialize — так теперь
/// зовётся свободная функция, кладущая строку в коробку (aggregate.hpp), и
/// одноимённая рекурсия рядом с ней читалась бы как её же вызов.
bool buildValue(const Ast &ast, std::string_view source, NodeId node,
                Store &store, Deferred &dead, Value *out, Diagnostic &diag) {
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
            // Данные от хоста целиком ложатся в глобальную переменную:
            // materialize кладёт короткую строку внутрь Value, а длинную — в
            // отдельно выделенный box.
            std::string scratch;
            *out = CS::materialize(literalText(ast, node, source, scratch), dead);
            return true;
        }

        case NodeKind::Unary: {
            // Минус над числом — это запись отрицательного значения, а не
            // вычисление: знака в NumericLiteral нет (docs/grammar.md §4.6),
            // и без этой ветки первое же отрицательное поле с бэкенда упёрлось
            // бы в «выражения в данных запрещены».
            if (ast.op(node) != TokenKind::Minus) {
                return rejectNode(ast, node, diag);
            }
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
            const Value array = CS::makeArray(count, store.clock(), dead);
            for (std::uint32_t i = 0; i < count; ++i) {
                Value element = Value::null();
                if (!buildValue(ast, source, ast.child(node, i), store, dead, &element, diag)) {
                    return false;
                }
                arrayPush(array, element);
            }
            *out = array;
            return true;
        }

        case NodeKind::Object: {
            // Дети чередуются: ключ, значение, ключ, значение.
            const std::uint32_t count = ast.childCount(node);
            const Value object = CS::makeObject(store.keys(), count / 2, store.clock(), dead);
            std::string scratch;
            for (std::uint32_t i = 0; i + 1 < count; i += 2) {
                Value value = Value::null();
                if (!buildValue(ast, source, ast.child(node, i + 1), store, dead, &value, diag)) {
                    return false;
                }
                // Повторный ключ заменяет значение: последний выигрывает.
                objectSet(object,
                          literalText(ast, ast.child(node, i), source, scratch),
                          value, dead);
            }
            *out = object;
            return true;
        }

        default:
            return rejectNode(ast, node, diag);
    }
}

}  // namespace

bool isGlobalName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 0xffffffffu) { return false; }

    Lexer lexer(name.data(), static_cast<std::uint32_t>(name.size()));
    Diagnostic ignored;

    Token first;
    if (!lexer.next(first, ignored)) { return false; }
    if (first.kind != TokenKind::Identifier) { return false; }
    // Токен обязан покрывать имя целиком: иначе " state" и "state " прошли бы,
    // а обратиться к такой глобальной переменной нельзя.
    if (first.offset != 0 || first.length != name.size()) { return false; }

    Token tail;
    if (!lexer.next(tail, ignored)) { return false; }
    return tail.kind == TokenKind::End;
}

bool setVariable(Store &store, Deferred &dead, std::string_view name,
                 std::string_view text, Diagnostic &diag) {
    if (!isGlobalName(name)) {
        diag = Diagnostic{ErrorCode::Name, 0, "global name must be an identifier"};
        return false;
    }

    // Та же защита, что в isGlobalName: длина обязана влезать в std::uint32_t,
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
    if (!buildValue(ast, text, ast.root(), store, dead, &value, diag)) { return false; }

    // Корень заводится только после успеха: отказ не оставляет имени.
    store.setGlobal(name, value, dead);
    return true;
}

}  // namespace CS
