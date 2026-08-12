#include "builtin.hpp"

#include <algorithm>
#include <cassert>

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
    {"typeof", 1, 1, true},
};

constexpr std::size_t kCount = sizeof kTable / sizeof kTable[0];
static_assert(kCount == static_cast<std::size_t>(Builtin::Typeof) + 1,
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

}  // namespace CS
