#include "printer.hpp"

#include <vector>

#include "text.hpp"

namespace chupa {
namespace {

/// Строковый литерал языка: одинарные кавычки и пять escape-последовательностей
/// из docs/grammar.md §4.3. Напечатанное обязано набираться обратно.
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

/// path — агрегаты, печатаемые прямо сейчас: путь от корня до текущего места.
void append(std::string &out, const CS::Context &ctx, CS::Value value,
            std::vector<CS::Value> &path) {
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
    path.push_back(value);

    if (value.kind() == CS::Value::Kind::Array) {
        out += '[';
        const std::uint32_t count = ctx.arrayCount(value);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (i != 0) { out += ", "; }
            append(out, ctx, ctx.arrayAt(value, i), path);
        }
        out += ']';
    } else {
        out += '{';
        const std::uint32_t count = ctx.objectCount(value);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (i != 0) { out += ", "; }
            appendQuoted(out, ctx.objectKeyAt(value, i));
            out += ": ";
            append(out, ctx, ctx.objectValueAt(value, i), path);
        }
        out += '}';
    }

    path.pop_back();
}

}  // namespace

std::string printValue(const CS::Context &ctx, CS::Value value) {
    std::string out;
    std::vector<CS::Value> path;
    append(out, ctx, value, path);
    return out;
}

}  // namespace chupa
