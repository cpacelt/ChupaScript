#pragma once
#include <cstdint>
#include <string_view>

#include "box.hpp"
#include "keytable.hpp"
#include "value.hpp"

namespace CS {

/// Чтение агрегата.
///
/// Свободные функции, а не методы хранилища, и это не вкусовщина. С приходом
/// коробок агрегат стал самодостаточным: элементы лежат в нём самом, имена
/// полей — в таблице, которую он держит своей ссылкой. Ни одного члена
/// хранилища эти функции не читали и раньше — методами они оставались по
/// наследству от арен, когда агрегат правда был диапазоном в чужом пуле.
///
/// Цена наследства была не косметической. Чтобы позвать метод, вычислителю
/// приходилось сперва выбрать хранилище — Execution::storeOf, — а выбор этот
/// у агрегата предрешён: регион у него всегда Boxed, и ветка всегда шла в одну
/// сторону. Развилка была, выбора не было.
///
/// Отсюда же и то, ради чего всё затевалось: прочитать агрегат вправе кто
/// угодно и когда угодно, в том числе когда контекста, породившего его, уже
/// нет. Функция, которой нечего спросить у хранилища, это свойство выражает, а
/// метод — прятал.
///
/// Срез, отдаваемый наружу (objectKeyAt), переживает лишь до ближайшей записи
/// в таблицу имён.

/// Предусловие: a.kind() == Value::Kind::Array.
[[nodiscard]] inline std::uint32_t arrayCount(Value a) noexcept {
    assert(a.kind() == Value::Kind::Array);
    return static_cast<std::uint32_t>(
        static_cast<const detail::ArrayBox *>(a.box())->items.size());
}

/// Элемент либо null за границей (docs/semantics.md §6.1).
/// Предусловие: a.kind() == Value::Kind::Array.
[[nodiscard]] inline Value arrayAt(Value a, std::uint32_t index) noexcept {
    assert(a.kind() == Value::Kind::Array);
    const detail::ArrayBox *box = static_cast<const detail::ArrayBox *>(a.box());
    if (index >= box->items.size()) { return Value::null(); }
    // Ссылка не берётся: значение живо, пока его держит сам массив, а массив
    // держит тот, кто его читает. Кадр скролла к счётчику не обращается.
    return box->items[index];
}

/// Предусловие: o.kind() == Value::Kind::Object.
[[nodiscard]] inline std::uint32_t objectCount(Value o) noexcept {
    assert(o.kind() == Value::Kind::Object);
    return static_cast<std::uint32_t>(
        static_cast<const detail::ObjectBox *>(o.box())->entries.size());
}

/// Значение либо null, если ключа нет (docs/semantics.md §6.2).
/// Предусловие: o.kind() == Value::Kind::Object.
[[nodiscard]] inline Value objectGet(Value o, std::string_view key) noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectBox &box = *static_cast<const detail::ObjectBox *>(o.box());
    bool found = false;
    const std::uint32_t at = detail::findEntry(box, key, &found);
    if (!found) { return Value::null(); }
    return box.entries[at].value;
}

/// Есть ли ключ. Отличает записанный null от отсутствия — иначе их не
/// различить (docs/semantics.md §6.2, §8.3).
/// Предусловие: o.kind() == Value::Kind::Object.
[[nodiscard]] inline bool objectHas(Value o, std::string_view key) noexcept {
    assert(o.kind() == Value::Kind::Object);
    bool found = false;
    detail::findEntry(*static_cast<const detail::ObjectBox *>(o.box()), key, &found);
    return found;
}

/// Ключ по порядковому номеру либо пустой срез за границей. Порядок
/// перечисления наружу не обещан (docs/semantics.md §2.1).
/// Предусловие: o.kind() == Value::Kind::Object.
[[nodiscard]] inline std::string_view objectKeyAt(Value o,
                                                  std::uint32_t i) noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectBox &box = *static_cast<const detail::ObjectBox *>(o.box());
    if (i >= box.entries.size()) { return {}; }
    return box.keys->bytes(box.entries[i].key);
}

/// Значение по порядковому номеру либо null за границей.
/// Предусловие: o.kind() == Value::Kind::Object.
[[nodiscard]] inline Value objectValueAt(Value o, std::uint32_t i) noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectBox &box = *static_cast<const detail::ObjectBox *>(o.box());
    if (i >= box.entries.size()) { return Value::null(); }
    return box.entries[i].value;
}

}  // namespace CS
