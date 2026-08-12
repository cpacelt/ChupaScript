#include "eval.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
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

/// Приведение скаляра к строке (docs/semantics.md §4).
///
/// Возвращает срез: у строки — её собственные байты в контексте, у числа —
/// буфер вызывающего, у остальных — статическая строка. В контекст ничего не
/// кладётся: строка нужна на время одного поиска ключа, и класть её в пул
/// значило бы копить мусор на каждом чтении.
///
/// numberBuffer обязан быть размером не меньше kNumberBufferSize.
bool coerceToString(const Ast &ast, NodeId node, Context &ctx, Value value,
                    char *numberBuffer, std::string_view *out,
                    Diagnostic &diag) {
    switch (value.kind()) {
        case Value::Kind::String:
            *out = ctx.string(value);
            return true;
        case Value::Kind::Boolean:
            *out = value.booleanValue() ? "true" : "false";
            return true;
        case Value::Kind::Null:
            *out = "null";
            return true;
        case Value::Kind::Number:
            *out = formatNumber(value.numberValue(), numberBuffer,
                                kNumberBufferSize);
            return true;
        default:
            return fail(ast, node, ErrorCode::Type,
                        "aggregates cannot be converted to string", diag);
    }
}

/// Чтение элемента массива (docs/semantics.md §6.1).
bool readIndex(const Ast &ast, NodeId node, Context &ctx, Value array,
               Value subscript, Value *out, Diagnostic &diag) {
    if (subscript.kind() != Value::Kind::Number) {
        return fail(ast, node, ErrorCode::Type, "array index must be a number",
                    diag);
    }

    const double index = subscript.numberValue();
    // Дробный и отрицательный индекс — ошибка автора, а не неполнота данных:
    // приведения к целому в языке нет.
    if (!std::isfinite(index) || index < 0.0 || index != std::floor(index)) {
        return fail(ast, node, ErrorCode::Range,
                    "array index must be a non-negative integer", diag);
    }

    // За границей — штатное чтение. Сравнение в double, потому что индекс
    // может превышать всё, что влезает в uint32.
    if (index >= static_cast<double>(ctx.arrayCount(array))) {
        *out = Value::null();
        return true;
    }
    *out = ctx.arrayAt(array, static_cast<std::uint32_t>(index));
    return true;
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

        case NodeKind::Index: {
            Value base = Value::null();
            if (!eval(ast, ast.child(node, 0), ctx, &base, diag)) { return false; }
            // Индекс вычисляется даже при базе null: порядок зафиксирован
            // (docs/semantics.md §3.3), а короткого замыкания у индексации нет.
            Value subscript = Value::null();
            if (!eval(ast, ast.child(node, 1), ctx, &subscript, diag)) {
                return false;
            }

            switch (base.kind()) {
                case Value::Kind::Array:
                    return readIndex(ast, node, ctx, base, subscript, out, diag);
                case Value::Kind::Object: {
                    char buffer[kNumberBufferSize];
                    std::string_view key;
                    if (!coerceToString(ast, node, ctx, subscript, buffer, &key,
                                        diag)) {
                        return false;
                    }
                    return readKey(ast, node, ctx, base, key, out, diag);
                }
                case Value::Kind::Null:
                    *out = Value::null();
                    return true;
                default:
                    return fail(ast, node, ErrorCode::Type,
                                "only arrays and objects can be indexed", diag);
            }
        }

        case NodeKind::Array: {
            const std::uint32_t count = ast.childCount(node);
            // Размер известен заранее — точное выделение, без переездов.
            const Value array = ctx.makeArray(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                Value element = Value::null();
                if (!eval(ast, ast.child(node, i), ctx, &element, diag)) {
                    return false;
                }
                ctx.arrayPush(array, element);
            }
            *out = array;
            return true;
        }

        case NodeKind::Object: {
            // Дети чередуются: ключ, значение. Ключ — строковый литерал по
            // грамматике, приведение §4 к нему не применяется.
            const std::uint32_t count = ast.childCount(node);
            const Value object = ctx.makeObject(count / 2);
            std::string scratch;
            for (std::uint32_t i = 0; i + 1 < count; i += 2) {
                Value value = Value::null();
                if (!eval(ast, ast.child(node, i + 1), ctx, &value, diag)) {
                    return false;
                }
                ctx.objectSet(object,
                              literalText(ast, ast.child(node, i), scratch),
                              value);
            }
            *out = object;
            return true;
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
