#pragma once
#include <cstdint>
#include <string_view>

#include "builtin_id.hpp"
#include "diagnostic.hpp"
#include "execution.hpp"
#include "store.hpp"
#include "value.hpp"

namespace CS {

/// Верхняя граница числа аргументов отсутствует. Только у format (§8.8).
inline constexpr std::uint8_t kVariadic = 255;

/// Наибольшая фиксированная арность среди встроенных функций.
///
/// По ней размерен буфер аргументов в вычислителе. format вариадичен и идёт
/// мимо буфера — его аргументы вычисляются по мере надобности.
inline constexpr std::uint8_t kMaxFixedArgs = 2;

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

/// Вид одного куска, на которые format-шаблон распадается при разборе слева
/// направо (docs/semantics.md §8.8).
enum class FormatPiece : std::uint8_t {
    Literal,      ///< пробег без плейсхолдеров — копируется как есть
    Placeholder,  ///< ${} — потребляет следующий аргумент
    Escaped,      ///< $${} — литеральное ${}, аргумента не требует
};

/// Позиция разбора шаблона. Значение по умолчанию — самое начало.
struct FormatCursor {
    std::size_t pos = 0;
};

/// Разбирает следующий кусок шаблона начиная с cursor.pos и продвигает
/// cursor. false — шаблон исчерпан; *piece и *text не трогаются.
///
/// Единственное место, где записано правило «$${} даёт литеральное ${} и
/// плейсхолдером не считается» (docs/semantics.md §8.8): и countPlaceholders
/// ниже, и format (core/src/eval.cpp) разбирают шаблон только через эту
/// функцию — раньше у них было по своей копии этого правила, и совпадение
/// держалось на аккуратности, а не на устройстве.
///
/// fmt передаётся при каждом вызове, а не хранится внутри cursor: у format
/// вычисление аргумента-плейсхолдера вправе дописать в тот же пул текста, где
/// лежит шаблон (строковый литерал, str, вложенный format — все кладут байты
/// в хранилище), и переезд пула отправит закэшированный срез в никуда. Свежий
/// fmt на каждый вызов — единственный способ пережить это, а cursor.pos как
/// голое число переезду вообще не подвержен.
bool nextFormatPiece(std::string_view fmt, FormatCursor &cursor,
                     FormatPiece *piece, std::string_view *text) noexcept;

/// Сколько плейсхолдеров ${} в шаблоне; $${} даёт литеральное ${} и не считается
/// (docs/semantics.md §8.8).
///
/// Одна функция на два потребителя: статический проход сверяет ею число
/// аргументов при литеральном шаблоне, вычислитель ею же разбирает шаблон при
/// сборке строки. Правило записано один раз — в nextFormatPiece, а эта функция
/// лишь считает его вывод.
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
///
/// exec, а не хранилище, потому что три роли здесь разные: аргумент читается
/// тем хранилищем, которое его выдало (Execution::storeOf), результат создаётся
/// во временном регионе, а push пишет туда, где лежит его цель — и потому
/// единственный зовёт promote (docs/backlog.md [B57]).
bool applyBuiltin(Builtin id, Execution &exec, const Value *args,
                  std::uint32_t count, std::uint32_t offset, Value *out,
                  Diagnostic &diag);

/// Приводит скаляр к строке по docs/semantics.md §4. Агрегат — ошибка.
///
/// Одна функция на всех потребителей: ключ объекта (§4.1, has, индексация
/// объекта), str и format (задачи 5 и 6), приведение в обходе (eval.cpp).
/// Правило §4 записано один раз.
///
/// Возвращает срез: у строки — её собственные байты в хранилище, у числа —
/// numberBuffer вызывающего (обязан быть размером не меньше
/// kNumberBufferSize, core/src/text.hpp), у остальных — статическая строка.
/// offset — место, куда указывает диагностика при отказе.
bool coerceScalarToString(const Store &store, Value v, char *numberBuffer,
                          std::string_view *out, std::uint32_t offset,
                          Diagnostic &diag);

}  // namespace CS
