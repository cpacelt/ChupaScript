#pragma once
#include <cstdint>
#include <string_view>

#include "box.hpp"
#include "deferred.hpp"
#include "epoch.hpp"
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
    return static_cast<const detail::ArrayBox *>(a.box())->size();
}

/// Элемент либо null за границей (docs/semantics.md §6.1).
/// Предусловие: a.kind() == Value::Kind::Array.
[[nodiscard]] inline Value arrayAt(Value a, std::uint32_t index) noexcept {
    assert(a.kind() == Value::Kind::Array);
    const detail::ArrayBox *box = static_cast<const detail::ArrayBox *>(a.box());
    if (index >= box->size()) { return Value::null(); }
    // Ссылка не берётся: значение живо, пока его держит сам массив, а массив
    // держит тот, кто его читает. Кадр скролла к счётчику не обращается.
    return box->at(index);
}

/// Предусловие: o.kind() == Value::Kind::Object.
[[nodiscard]] inline std::uint32_t objectCount(Value o) noexcept {
    assert(o.kind() == Value::Kind::Object);
    return static_cast<const detail::ObjectBox *>(o.box())->size();
}

/// Значение либо null, если ключа нет (docs/semantics.md §6.2).
/// Предусловие: o.kind() == Value::Kind::Object.
[[nodiscard]] inline Value objectGet(Value o, std::string_view key) noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectBox &box = *static_cast<const detail::ObjectBox *>(o.box());
    bool found = false;
    const std::uint32_t at = detail::findEntry(box, key, &found);
    if (!found) { return Value::null(); }
    return box.at(at).value;
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
    if (i >= box.size()) { return {}; }
    return box.keys->bytes(box.at(i).key);
}

/// Значение по порядковому номеру либо null за границей.
/// Предусловие: o.kind() == Value::Kind::Object.
[[nodiscard]] inline Value objectValueAt(Value o, std::uint32_t i) noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectBox &box = *static_cast<const detail::ObjectBox *>(o.box());
    if (i >= box.size()) { return Value::null(); }
    return box.at(i).value;
}

// ─── создание ───
//
// Тоже свободные, и по той же причине, что и всё выше: у коробки нет
// хранилища-владельца. Раньше эти две были методами Store и трогали в нём
// ровно два члена — таблицу имён и список отложенного освобождения, — и оба
// приходилось держать во **временном** хранилище дубликатом только ради них.
// Таблица теперь приходит параметром от того, кто её и так держит, а ссылка
// создателя — в список, которому она с самого начала и предназначалась.

/// Turns bytes into a string value.
///
///   <= Value::kInlineCapacity  ->  the bytes go inside the value; no box,
///                                  no reference count, no allocation
///   longer                     ->  a box, whose creator reference goes to
///                                  dead and is dropped at the next boundary
///
/// dead is untouched on the inline path, and that is the point: the hot BDUI
/// cases — a colour, a key, an identifier, str() over a number — stop
/// allocating entirely.
[[nodiscard]] inline Value materialize(std::string_view bytes, Deferred &dead) {
    if (bytes.size() <= Value::kInlineCapacity) {
        return Value::inlineString(bytes);
    }
    detail::StringBox *box = detail::makeStringBox(bytes);
    dead.take(box);  // ссылка создателя — до ближайшей границы
    return Value::string(box);
}

/// Создаёт пустой массив. capacity — сколько элементов выделить заранее; на
/// длину не влияет, элементы добавляет только arrayPush.
///
/// clock — лента контекста: рождение берёт номер оттуда же, откуда мутация.
/// Параметром, а не из хранилища: коробке хранилище не нужно ни для чего, и
/// это свойство здесь ценится — прочитать агрегат вправе кто угодно, в том
/// числе когда контекста уже нет. Лента нужна только на запись, а записи
/// снаружи контекста в языке не существует.
///
/// Ссылка создателя уходит в dead: агрегат, который никто не удержал, умрёт на
/// ближайшей границе операции, а не повиснет.
[[nodiscard]] inline Value makeArray(std::uint32_t capacity, EpochClock &clock,
                                     Deferred &dead) {
    detail::ArrayBox *box = detail::makeArrayBox(capacity, clock.tick());
    dead.take(box);  // ссылка создателя — до ближайшей границы
    return Value::array(box);
}

/// Создаёт пустой объект. capacity — сколько пар выделить заранее.
///
/// keys — таблица, в которую коробка будет интернировать имена своих полей и
/// которую удержит своей ссылкой. Даёт её тот, кто таблицей владеет, —
/// постоянное хранилище контекста.
///
/// clock — та же лента, что и у makeArray: см. её обоснование там.
[[nodiscard]] inline Value makeObject(KeyTable *keys, std::uint32_t capacity,
                                      EpochClock &clock, Deferred &dead) {
    detail::ObjectBox *box = detail::makeObjectBox(keys, capacity, clock.tick());
    dead.take(box);  // ссылка создателя — до ближайшей границы
    return Value::object(box);
}

// ─── изменение ───
//
// Вытесненная ссылка уходит в Deferred, а не освобождается на месте: см.
// deferred.hpp. Больше мутатору от окружения ничего не нужно — ни пула, ни
// таблицы имён хранилища: имена он берёт у таблицы самой коробки.

/// Заменяет элемент. false за границей — по docs/semantics.md §7.2 это ошибка,
/// диагностику формулирует вызывающий.
/// Предусловие: a.kind() == Value::Kind::Array.
inline bool arraySet(Value a, std::uint32_t index, Value v,
                     Deferred &dead) noexcept {
    assert(a.kind() == Value::Kind::Array);
    detail::ArrayBox *box = static_cast<detail::ArrayBox *>(a.box());
    if (index >= box->size()) { return false; }
    detail::retainValue(v);
    dead.take(box->at(index));
    box->set(index, v);
    return true;
}

/// Добавляет элемент в конец. Единственный способ расширить массив
/// (docs/semantics.md §6.1). Вытеснять нечего, поэтому список не нужен.
/// Предусловие: a.kind() == Value::Kind::Array.
inline void arrayPush(Value a, Value v) {
    assert(a.kind() == Value::Kind::Array);
    detail::retainValue(v);
    static_cast<detail::ArrayBox *>(a.box())->push(v);
}

/// Снимает последний элемент в *out. false на пустом массиве; выходной
/// параметр при отказе не меняется. out допускает nullptr.
/// Предусловие: a.kind() == Value::Kind::Array.
inline bool arrayPop(Value a, Value *out, Deferred &dead) noexcept {
    assert(a.kind() == Value::Kind::Array);
    detail::ArrayBox *box = static_cast<detail::ArrayBox *>(a.box());
    if (box->size() == 0) { return false; }
    const Value last = box->pop();
    if (out != nullptr) { *out = last; }
    // Ссылка ячейки уходит в список, а не в release: вызывающий читает снятое
    // значение сразу после возврата.
    dead.take(last);
    return true;
}

/// Записывает значение по ключу: заменяет существующее либо создаёт ключ
/// (docs/semantics.md §6.2). Байты нового ключа интернируются в таблице самой
/// коробки.
/// Предусловие: o.kind() == Value::Kind::Object.
inline void objectSet(Value o, std::string_view key, Value v, Deferred &dead) {
    assert(o.kind() == Value::Kind::Object);
    detail::ObjectBox &box = *static_cast<detail::ObjectBox *>(o.box());

    const std::uint32_t prefix = detail::keyPrefix(key);
    bool found = false;
    const std::uint32_t at = detail::findEntry(box, key, prefix, &found);
    detail::retainValue(v);
    if (found) {
        dead.take(box.at(at).value);
        box.setValue(at, v);
        return;
    }
    // Интернируется только тот ключ, который правда заводится: чтение
    // отсутствующего имени таблицу не засоряет — за этим следит findEntry,
    // который сравнивает байты и в таблицу не пишет.
    box.insert(at, detail::Entry{box.keys->intern(key), prefix, v});
}

}  // namespace CS
