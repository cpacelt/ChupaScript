#pragma once
#include <cstdint>
#include <string_view>

namespace CS {

/// Встроенные функции языка (docs/semantics.md §8).
///
/// Порядок совпадает с алфавитным порядком имён: таблица ищется двоично.
enum class Builtin : std::uint8_t {
    Abs, Count, Format, Has, Keys, Last, Max, Min, Pop, Push, Round, Str, Typeof
};

/// Верхняя граница числа аргументов отсутствует. Только у format (§8.9).
inline constexpr std::uint8_t kVariadic = 255;

/// Что проходу и вычислителю нужно знать о функции, не вызывая её.
struct BuiltinInfo {
    std::string_view name;
    std::uint8_t minArgs;
    std::uint8_t maxArgs;   ///< kVariadic — без верхней границы
    bool returnsValue;      ///< false — Void (§2.2): результат использовать нельзя
};

/// Находит функцию по имени. false — такой функции нет.
bool findBuiltin(std::string_view name, Builtin *out) noexcept;

const BuiltinInfo &builtinInfo(Builtin id) noexcept;

/// Сколько плейсхолдеров ${} в шаблоне; $${} даёт литеральное ${} и не считается
/// (docs/semantics.md §8.9).
///
/// Одна функция на два потребителя: статический проход сверяет ею число
/// аргументов при литеральном шаблоне, вычислитель ею же разбирает шаблон при
/// сборке строки. Правило записано один раз.
std::uint32_t countPlaceholders(std::string_view fmt) noexcept;

}  // namespace CS
