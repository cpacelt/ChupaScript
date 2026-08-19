#pragma once
#include <cstdint>
#include <string_view>

#include "box.hpp"
#include "deferred.hpp"
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

// ─── создание ───
//
// Тоже свободные, и по той же причине, что и всё выше: у коробки нет
// хранилища-владельца. Раньше эти две были методами Store и трогали в нём
// ровно два члена — таблицу имён и список отложенного освобождения, — и оба
// приходилось держать во **временном** хранилище дубликатом только ради них.
// Таблица теперь приходит параметром от того, кто её и так держит, а ссылка
// создателя — в список, которому она с самого начала и предназначалась.

/// Кладёт байты в коробку со счётчиком.
///
/// Нужна там, где строке предстоит пережить операцию: лечь в агрегат, в ячейку
/// глобальной переменной либо уехать к хосту. Смещение в арену туда не годится
/// — арену граница сбрасывает.
///
/// Свободная по той же причине, что и всё вокруг: ни одного члена хранилища
/// она не читала и раньше. Методом Store она оставалась ровно затем, чтобы
/// достать список отложенного освобождения, — а тот теперь приходит
/// параметром от выполнения, которому и принадлежит.
[[nodiscard]] inline Value materialize(std::string_view bytes, Deferred &dead) {
    detail::StringBox *box = detail::makeStringBox(bytes);
    dead.take(box);  // ссылка создателя — до ближайшей границы
    return Value::string(box);
}

/// Создаёт пустой массив. capacity — сколько элементов выделить заранее; на
/// длину не влияет, элементы добавляет только arrayPush.
///
/// Ссылка создателя уходит в dead: агрегат, который никто не удержал, умрёт на
/// ближайшей границе операции, а не повиснет.
[[nodiscard]] inline Value makeArray(std::uint32_t capacity, Deferred &dead) {
    detail::ArrayBox *box = detail::makeArrayBox(capacity);
    dead.take(box);  // ссылка создателя — до ближайшей границы
    return Value::array(box);
}

/// Создаёт пустой объект. capacity — сколько пар выделить заранее.
///
/// keys — таблица, в которую коробка будет интернировать имена своих полей и
/// которую удержит своей ссылкой. Даёт её тот, кто таблицей владеет, —
/// постоянное хранилище контекста.
[[nodiscard]] inline Value makeObject(KeyTable *keys, std::uint32_t capacity,
                                      Deferred &dead) {
    detail::ObjectBox *box = detail::makeObjectBox(keys, capacity);
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
    if (index >= box->items.size()) { return false; }
    detail::retainValue(v);
    dead.take(box->items[index]);
    box->items[index] = v;
    return true;
}

/// Добавляет элемент в конец. Единственный способ расширить массив
/// (docs/semantics.md §6.1). Вытеснять нечего, поэтому список не нужен.
/// Предусловие: a.kind() == Value::Kind::Array.
inline void arrayPush(Value a, Value v) {
    assert(a.kind() == Value::Kind::Array);
    detail::retainValue(v);
    static_cast<detail::ArrayBox *>(a.box())->items.push_back(v);
}

/// Снимает последний элемент в *out. false на пустом массиве; выходной
/// параметр при отказе не меняется. out допускает nullptr.
/// Предусловие: a.kind() == Value::Kind::Array.
inline bool arrayPop(Value a, Value *out, Deferred &dead) noexcept {
    assert(a.kind() == Value::Kind::Array);
    detail::ArrayBox *box = static_cast<detail::ArrayBox *>(a.box());
    if (box->items.empty()) { return false; }
    const Value last = box->items.back();
    box->items.pop_back();
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
        dead.take(box.entries[at].value);
        box.entries[at].value = v;
        return;
    }
    // Интернируется только тот ключ, который правда заводится: чтение
    // отсутствующего имени таблицу не засоряет — за этим следит findEntry,
    // который сравнивает байты и в таблицу не пишет.
    box.entries.insert(box.entries.begin() + at,
                       detail::Entry{box.keys->intern(key), prefix, v});
}

}  // namespace CS
