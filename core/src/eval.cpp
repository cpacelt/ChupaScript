#include "eval.hpp"

#include <cassert>
#include <string>
#include <string_view>

#include "text.hpp"

namespace CS {
namespace {

// Предварительное объявление для readKey
bool eval(const Ast &ast, NodeId node, Context &ctx, Value *out,
          Diagnostic &diag);

/// Записывает отказ с местом узла. Первая ошибка выигрывает: вызывающие
/// возвращают false немедленно и диагностику не переписывают.
bool fail(const Ast &ast, NodeId node, ErrorCode code, const char *message,
          Diagnostic &diag) {
    diag = Diagnostic{code, ast.offset(node), message};
    return false;
}

/// Чтение ключа у базы (docs/semantics.md §6.2, §6.3, §6.4).
///
/// Объект — значение либо null; null — null; прочее — ошибка. Один и тот же
/// разбор обслуживает и obj.k, и obj[k]: отличаются они только тем, откуда
/// берётся ключ.
bool readKey(const Ast &ast, NodeId node, Context &ctx, Value base,
             std::string_view key, Value *out, Diagnostic &diag) {
    switch (base.kind()) {
        case Value::Kind::Object:
            *out = ctx.objectGet(base, key);
            return true;
        case Value::Kind::Null:
            *out = Value::null();
            return true;
        default:
            return fail(ast, node, ErrorCode::Type, "only objects have keys",
                        diag);
    }
}

/// Обход дерева. Рекурсия, а не цикл: короткому замыканию нужен пропуск
/// поддеревьев, а циклу — буфер значений на всё дерево (спека §3). Собственного
/// предела глубины нет — её ограничил парсер.
bool eval(const Ast &ast, NodeId node, Context &ctx, Value *out,
          Diagnostic &diag) {
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

        case NodeKind::Identifier: {
            // docs/semantics.md §7.1: объявлений в языке нет, всякий
            // идентификатор — чтение из контекста.
            const std::string_view name = ast.text(node);
            // Неизвестный корень — ошибка, а не null: состав корней контексту
            // известен, состав ключей внутри них — нет. Поэтому опечатка в
            // корне ловится, а опечатка глубже даёт null по §6.3.
            if (!ctx.hasRoot(name)) {
                return fail(ast, node, ErrorCode::Name, "unknown root", diag);
            }
            *out = ctx.root(name);
            return true;
        }

        case NodeKind::Member: {
            Value base = Value::null();
            if (!eval(ast, ast.child(node, 0), ctx, &base, diag)) { return false; }
            // Имя поля берётся из узла буквально, без приведения.
            return readKey(ast, node, ctx, base, ast.text(node), out, diag);
        }

        default:
            // Часть 1 не знает операторов и вызовов. С приходом частей 2 и 3
            // ветка сузится до Program, Assign и CallStatement — узлов, которых
            // в дереве от parseExpression быть не может, — и станет защитной.
            return fail(ast, node, ErrorCode::Type,
                        "expression form is not supported", diag);
    }
}

}  // namespace

bool evalExpression(const Ast &ast, Context &ctx, Value *out,
                    Diagnostic &diag) {
    assert(ast.root() != kNoNode && "дерево обязано быть разобрано успешно");
    return eval(ast, ast.root(), ctx, out, diag);
}

}  // namespace CS
