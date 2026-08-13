#include "printer.hpp"

#include <vector>

#include "text.hpp"

namespace chupa {
namespace {

/// Строковый литерал языка: одинарные кавычки. Грамматика (docs/grammar.md
/// §4.7) знает пять escape-последовательностей, но эта функция всегда печатает
/// в одинарных кавычках, а `\"` внутри них не нужен — поэтому здесь их четыре,
/// и это не расхождение с грамматикой, а следствие фиксированного выбора
/// кавычки. Напечатанное обязано набираться обратно.
void appendQuoted(std::string &out, std::string_view text) {
    out += '\'';
    for (const char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\'': out += "\\'"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    out += '\'';
}

/// Предел глубины печати: агрегат глубже него печатается как `[...]` либо
/// `{...}` — той же меткой, что и цикл, — вместо содержимого.
///
/// Не замена ловле цикла путём ниже, а дополнение к ней: цикл ловится путём
/// (агрегат, уже стоящий на пути печати), глубина — отдельным счётчиком, и это
/// разные средства против разных бед. Нецикличное, но достаточно высокое
/// дерево путь не поймает — оно ни разу не повторяет агрегат, — и без счётчика
/// глубины печать переполнила бы стек раньше, чем нашла бы цикл, которого там
/// нет. docs/superpowers/specs/2026-08-13-chupa-repl-design.md §6 возражает
/// против предела глубины вместо отслеживания пути — и остаётся право: путь
/// нужен ловить цикл, счётчик — не пустить в потолок стека нецикличное дерево.
///
/// 64 — с запасом на данные, приходящие с сервера (там глубже двадцати уровней
/// не бывает), и далеко от предела стека печатника.
constexpr std::uint32_t kMaxPrintDepth = 64;

/// path — агрегаты, печатаемые прямо сейчас: путь от корня до текущего места.
///
/// Инвариант глубины ложен, и раньше здесь было записано обратное. Дерево
/// значений НЕ наследует предел высоты дерева разбора (`kMaxDepth` в
/// `core/src/parser.cpp`): единственный путь данных в Context — не
/// `setVariable`. Вычислитель кладёт в Context уже существующие агрегаты:
/// литерал агрегата в выражении, присваивания `a.k = v` и `a[i] = v`, `push` —
/// и `push(a, b)` вкладывает в `a` агрегат `b` целиком, какой бы высоты тот ни
/// достиг к этой строке сессии (аналогично `a.k = b`). Высота дерева значений
/// поэтому растёт между строками сессии и с высотой дерева разбора одной
/// строки не связана (docs/backlog.md B5). Печатник поэтому ограничивает
/// глубину сам — `kMaxPrintDepth` выше.
void append(std::string &out, const CS::Context &ctx, CS::Value value,
            std::vector<CS::Value> &path, std::uint32_t depth) {
    switch (value.kind()) {
        case CS::Value::Kind::Null: out += "null"; return;
        case CS::Value::Kind::Boolean:
            out += value.booleanValue() ? "true" : "false";
            return;
        case CS::Value::Kind::Number: {
            char buffer[CS::kNumberBufferSize];
            out += CS::formatNumber(value.numberValue(), buffer, sizeof buffer);
            return;
        }
        case CS::Value::Kind::String:
            appendQuoted(out, ctx.string(value));
            return;
        default: break;
    }

    // Агрегат уже на пути — дальше идти значит зациклиться.
    for (const CS::Value &open : path) {
        if (open.sameAggregate(value)) {
            out += value.kind() == CS::Value::Kind::Array ? "[...]" : "{...}";
            return;
        }
    }

    // Дерево значений на пути не зациклилось, но может оказаться выше, чем
    // безопасно раскручивать стеком: см. комментарий к kMaxPrintDepth выше.
    if (depth >= kMaxPrintDepth) {
        out += value.kind() == CS::Value::Kind::Array ? "[...]" : "{...}";
        return;
    }
    path.push_back(value);

    if (value.kind() == CS::Value::Kind::Array) {
        out += '[';
        const std::uint32_t count = ctx.arrayCount(value);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (i != 0) { out += ", "; }
            append(out, ctx, ctx.arrayAt(value, i), path, depth + 1);
        }
        out += ']';
    } else {
        out += '{';
        const std::uint32_t count = ctx.objectCount(value);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (i != 0) { out += ", "; }
            appendQuoted(out, ctx.objectKeyAt(value, i));
            out += ": ";
            append(out, ctx, ctx.objectValueAt(value, i), path, depth + 1);
        }
        out += '}';
    }

    path.pop_back();
}

}  // namespace

std::string printValue(const CS::Context &ctx, CS::Value value) {
    std::string out;
    std::vector<CS::Value> path;
    append(out, ctx, value, path, 0);
    return out;
}

}  // namespace chupa
