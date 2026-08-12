#pragma once
#include <cstdint>
#include <string_view>

#include "context.hpp"
#include "diagnostic.hpp"
#include "value.hpp"

namespace CS {

/// Встроенные функции языка (docs/semantics.md §8).
///
/// Порядок совпадает с алфавитным порядком имён: таблица ищется двоично.
enum class Builtin : std::uint8_t {
    Abs, Count, Format, Has, Keys, Last, Max, Min, Pop, Push, Round, Str
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

/// Применяет функцию к уже вычисленным аргументам. Все, кроме format: он
/// вариадичен и вычисляет аргументы по мере надобности, поэтому его цикл живёт
/// в вычислителе (core/src/eval.cpp).
///
/// Число аргументов и то, что функция возвращает значение, гарантированы
/// статическим проходом (core/src/check.hpp): здесь они не перепроверяются.
/// Для Void-функций *out не трогается — Void не становится значением (§2.2).
///
/// offset — смещение узла вызова, для диагностики.
bool applyBuiltin(Builtin id, Context &ctx, const Value *args,
                  std::uint32_t count, std::uint32_t offset, Value *out,
                  Diagnostic &diag);

/// Приводит скаляр к строке по docs/semantics.md §4. Агрегат — ошибка.
///
/// Одна функция на всех потребителей: ключ объекта (§4.1, has, индексация
/// объекта), str и format (задачи 5 и 6), приведение в обходе (eval.cpp).
/// Правило §4 записано один раз.
///
/// Возвращает срез: у строки — её собственные байты в контексте, у числа —
/// numberBuffer вызывающего (обязан быть размером не меньше
/// kNumberBufferSize, core/src/text.hpp), у остальных — статическая строка.
/// offset — место, куда указывает диагностика при отказе.
bool coerceScalarToString(const Context &ctx, Value v, char *numberBuffer,
                          std::string_view *out, std::uint32_t offset,
                          Diagnostic &diag);

}  // namespace CS
