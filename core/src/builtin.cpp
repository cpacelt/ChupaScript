#include "builtin.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "text.hpp"

namespace CS {
namespace {

/// Отсортирована по имени: findBuiltin ищет двоично, как findKey в контексте.
/// Порядок обязан совпадать с порядком в enum Builtin — на этом стоит индексация
/// в builtinInfo, и тест BuiltinTable.IsSortedByName стережёт инвариант.
constexpr BuiltinInfo kTable[] = {
    {"abs", 1, 1, true},        {"count", 1, 1, true},
    {"format", 1, kVariadic, true}, {"has", 2, 2, true},
    {"keys", 1, 1, true},       {"last", 1, 1, true},
    {"max", 2, 2, true},        {"min", 2, 2, true},
    {"pop", 1, 1, false},       {"push", 2, 2, false},
    {"round", 1, 1, true},      {"str", 1, 1, true},
};

constexpr std::size_t kCount = sizeof kTable / sizeof kTable[0];
static_assert(kCount == static_cast<std::size_t>(Builtin::Str) + 1,
              "таблица и enum обязаны совпадать по составу");

}  // namespace

bool findBuiltin(std::string_view name, Builtin *out) noexcept {
    const BuiltinInfo *first = kTable;
    const BuiltinInfo *last = kTable + kCount;
    const BuiltinInfo *found = std::lower_bound(
        first, last, name,
        [](const BuiltinInfo &info, std::string_view key) {
            return info.name < key;
        });
    if (found == last || found->name != name) { return false; }
    *out = static_cast<Builtin>(found - first);
    return true;
}

const BuiltinInfo &builtinInfo(Builtin id) noexcept {
    const std::size_t index = static_cast<std::size_t>(id);
    assert(index < kCount);
    return kTable[index];
}

std::uint32_t countPlaceholders(std::string_view fmt) noexcept {
    std::uint32_t count = 0;
    std::size_t i = 0;
    while (i < fmt.size()) {
        // $${} — экранированный плейсхолдер: даёт литеральное ${}, но сам им
        // не является.
        if (fmt.compare(i, 4, "$${}") == 0) {
            i += 4;
            continue;
        }
        if (fmt.compare(i, 3, "${}") == 0) {
            ++count;
            i += 3;
            continue;
        }
        ++i;
    }
    return count;
}

namespace {

bool failType(std::uint32_t offset, const char *message, Diagnostic &diag) {
    diag = Diagnostic{ErrorCode::Type, offset, message};
    return false;
}

}  // namespace

namespace {

/// Все четыре арифметические функции требуют Number и отказывают одинаково.
bool numbersOnly(const Value *args, std::uint32_t count, const char *what,
                 std::uint32_t offset, Diagnostic &diag) {
    for (std::uint32_t i = 0; i < count; ++i) {
        if (args[i].kind() != Value::Kind::Number) {
            return failType(offset, what, diag);
        }
    }
    return true;
}

}  // namespace

bool coerceScalarToString(const Context &ctx, Value v, char *numberBuffer,
                          std::string_view *out, std::uint32_t offset,
                          Diagnostic &diag) {
    switch (v.kind()) {
        case Value::Kind::String: *out = ctx.string(v); return true;
        case Value::Kind::Number:
            *out = formatNumber(v.numberValue(), numberBuffer, kNumberBufferSize);
            return true;
        case Value::Kind::Boolean:
            *out = v.booleanValue() ? "true" : "false";
            return true;
        case Value::Kind::Null: *out = "null"; return true;
        default:
            // Одно сообщение на все позиции, требующие String (§4): ключ
            // объекта, str, format и приведение в обходе — это одно и то же
            // нарушение, а не частный случай ключа.
            return failType(offset, "aggregates cannot be converted to string",
                            diag);
    }
}

bool applyBuiltin(Builtin id, Context &ctx, const Value *args,
                  std::uint32_t count, std::uint32_t offset, Value *out,
                  Diagnostic &diag) {
    (void)count;  // арность гарантирована проходом
    switch (id) {
        case Builtin::Count:
            // Array, Object либо String (§8.1); у строки — байты, не символы.
            switch (args[0].kind()) {
                case Value::Kind::Array:
                    *out = Value::number(ctx.arrayCount(args[0]));
                    return true;
                case Value::Kind::Object:
                    *out = Value::number(ctx.objectCount(args[0]));
                    return true;
                case Value::Kind::String:
                    *out = Value::number(
                        static_cast<double>(ctx.string(args[0]).size()));
                    return true;
                default:
                    return failType(offset,
                                    "count expects an array, object or string",
                                    diag);
            }

        case Builtin::Keys: {
            if (args[0].kind() != Value::Kind::Object) {
                return failType(offset, "keys expects an object", diag);
            }
            const std::uint32_t size = ctx.objectCount(args[0]);
            // Точное выделение: длина известна заранее.
            Value result = ctx.makeArray(size);
            for (std::uint32_t i = 0; i < size; ++i) {
                // Порядок наружу не обещан (§8.2); мы отдаём тот, в котором
                // ключи лежат, и обещанием это не становится.
                ctx.arrayPush(result, ctx.makeString(ctx.objectKeyAt(args[0], i)));
            }
            *out = result;
            return true;
        }

        case Builtin::Has: {
            if (args[0].kind() != Value::Kind::Object) {
                return failType(offset, "has expects an object", diag);
            }
            char buffer[kNumberBufferSize];
            std::string_view key;
            if (!coerceScalarToString(ctx, args[1], buffer, &key, offset, diag)) {
                return false;
            }
            *out = Value::boolean(ctx.objectHas(args[0], key));
            return true;
        }

        case Builtin::Last: {
            if (args[0].kind() != Value::Kind::Array) {
                return failType(offset, "last expects an array", diag);
            }
            const std::uint32_t size = ctx.arrayCount(args[0]);
            // На пустом — null (§8.4): через индексацию это невыразимо.
            *out = size == 0 ? Value::null() : ctx.arrayAt(args[0], size - 1);
            return true;
        }

        case Builtin::Push:
            if (args[0].kind() != Value::Kind::Array) {
                return failType(offset, "push expects an array", diag);
            }
            // Void: *out не трогается (§2.2).
            ctx.arrayPush(args[0], args[1]);
            return true;

        case Builtin::Pop:
            if (args[0].kind() != Value::Kind::Array) {
                return failType(offset, "pop expects an array", diag);
            }
            // На пустом ничего не делает и не отказывает (§8.6). Снятое
            // значение никуда не идёт: pop его не возвращает.
            ctx.arrayPop(args[0], nullptr);
            return true;

        case Builtin::Str: {
            if (args[0].kind() == Value::Kind::String) {
                *out = args[0];
                return true;
            }
            char buffer[kNumberBufferSize];
            std::string_view text;
            // Агрегат отвергается тем же правилом §4, что и всюду: сообщение
            // общее, частных формулировок под каждый билтин не заводим.
            if (!coerceScalarToString(ctx, args[0], buffer, &text, offset, diag)) {
                return false;
            }
            *out = ctx.makeString(text);
            return true;
        }

        case Builtin::Min:
            if (!numbersOnly(args, 2, "min expects numbers", offset, diag)) {
                return false;
            }
            *out = Value::number(
                std::fmin(args[0].numberValue(), args[1].numberValue()));
            return true;

        case Builtin::Max:
            if (!numbersOnly(args, 2, "max expects numbers", offset, diag)) {
                return false;
            }
            *out = Value::number(
                std::fmax(args[0].numberValue(), args[1].numberValue()));
            return true;

        case Builtin::Abs:
            if (!numbersOnly(args, 1, "abs expects a number", offset, diag)) {
                return false;
            }
            *out = Value::number(std::fabs(args[0].numberValue()));
            return true;

        case Builtin::Round:
            if (!numbersOnly(args, 1, "round expects a number", offset, diag)) {
                return false;
            }
            // От нуля, а не к чётному (§8.11): ровно то, что делает std::round.
            *out = Value::number(std::round(args[0].numberValue()));
            return true;

        default:
            // Остальные функции приходят следующими задачами.
            return failType(offset, "builtin is not implemented yet", diag);
    }
}

}  // namespace CS
