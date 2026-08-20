#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "epoch.hpp"
#include "value.hpp"

namespace CS {

class KeyTable;

namespace detail {

/// Заголовок всякой коробки со счётчиком ссылок.
///
/// Коробка — это значение, вынесенное из ячейки Value наружу, потому что в
/// шестнадцать байт оно не помещается. Термин рантаймовый (boxing), и берётся
/// он в рантаймовом значении: **владение разделяемое**, а не единоличное, как
/// у Box<T> в Rust.
///
/// The reference count is intrusive and NOT atomic. A Context is the unit of
/// single-threadedness (chupascript.h, threading contract): one Context is
/// touched by at most one thread at a time, and a box is only ever reached
/// through the Context that created it or through a host handle the host is
/// responsible for. Making the count atomic would put a locked
/// read-modify-write on every retain and release along the single-threaded
/// path — which is every path we have measured.
///
/// Метаданных, диспетчеризации и боковых таблиц под weak тут
/// нет: видов ровно три, список закрыт спецификацией языка, и release
/// разбирает их switch'ем по kind. Виртуальных функций поэтому нет ни одной —
/// vtable удвоил бы заголовок с восьми байт до шестнадцати на каждом
/// агрегате в системе ради расширяемости, которой у языка не бывает.
///
/// Счётчик свой, а не shared_ptr, и причин тому четыре: тот шестнадцать байт
/// и в объединение Value (восемь) не влезает; ломает тривиальную
/// копируемость, на которой стоит static_assert; требует второй аллокации под
/// блок управления, а строке нужна одна, хвостом; считает атомарно.
///
/// Обоснование: docs/superpowers/specs/2026-08-19-memory-model-redesign-design.md Р4.
struct Box {
    std::uint32_t rc;
    Value::Kind kind;
};

/// Строка: заголовок и байты одной аллокацией, байты сразу за заголовком.
///
/// Члена под байты нет: массив переменной длины — расширение, а не стандарт.
/// Добираться до них надо через view(), больше ниоткуда они не видны.
struct StringBox : Box {
    std::uint32_t len;

    [[nodiscard]] std::string_view view() const noexcept;
};

/// An array: header and elements in one allocation, elements right behind the
/// header.
///
///   ArrayBox                              one allocation
///  ┌──────────────────────────────┐
///  │ rc, kind                     │ Box
///  │ len, cap                     │
///  │ data ──────────┐             │      points into the tail while cap
///  ├────────────────▼─────────────┤      suffices; at a separate buffer after
///  │ Value  Value  Value  ...     │      the array outgrows it
///  └──────────────────────────────┘      tail, cap slots
///
/// THE BOX'S ADDRESS NEVER CHANGES, not even once the array has outgrown the
/// tail: only data moves. Every Value pointing at this box therefore stays
/// correct — which is what growing the box itself could not offer.
///
/// A literal fits the tail exactly: its size is known before the box is made,
/// and makeArray is already called with the exact capacity. Growth happens
/// only to an array grown through push (docs/semantics.md §8.5).
struct ArrayBox : Box {
    /// Эпоха коробки — первым членом после общего заголовка, и в ObjectBox
    /// тоже первым: смещения совпадают намеренно и сторожатся static_assert
    /// в box.cpp, а читает эпоху разбор по виду в epochAddressOf (box.hpp) —
    /// не единый каст в ArrayBox, который был бы UB для ObjectBox.
    ///
    /// Наружу смещение не объявляется: адрес эпохи уезжает к читателю
    /// готовым, в Dep::epoch, и считать его хосту незачем. Так раскладка
    /// коробки остаётся приватной на всех трёх платформах.
    Epoch epoch;

    std::uint32_t len;
    std::uint32_t cap;
    Value *data;

    [[nodiscard]] std::uint32_t size() const noexcept { return len; }
    [[nodiscard]] const Value &at(std::uint32_t i) const noexcept {
        assert(i < len);
        return data[i];
    }
    void set(std::uint32_t i, Value v) noexcept { assert(i < len); data[i] = v; }
    void push(Value v);
    /// Precondition: len > 0.
    Value pop() noexcept { assert(len > 0); return data[--len]; }

    /// The tail's address. data equals it until the array outgrows the tail;
    /// after that the tail is dead space and data points at a separate buffer.
    [[nodiscard]] Value *tail() noexcept {
        return reinterpret_cast<Value *>(reinterpret_cast<char *>(this) +
                                         sizeof(ArrayBox));
    }
};

/// Первые до четырёх байт имени, упакованные big-endian и добитые нулями.
///
/// Порядок этих чисел совпадает с байтовым порядком самих имён — на этом и
/// стоит быстрый путь поиска. Доказательство короткое: пусть префиксы
/// различаются, и i — первый различающийся байт. Если оба имени длиннее i,
/// решает сравнение байт i, одно и то же в обоих порядках. Если одно имя
/// кончилось на i, у него там ноль, а у другого не ноль (иначе байты совпали
/// бы), — и оно же короче, то есть меньше и лексикографически.
///
/// Совпадение префиксов ничего не решает и отправляет на полное сравнение.
[[nodiscard]] std::uint32_t keyPrefix(std::string_view key) noexcept;

/// Пара объекта: номер ключа в таблице интернирования, префикс имени и
/// значение.
///
/// Префикс лежит в набивке, которая тут была и так: uint32 на нуле, Value с
/// восьмёрки по выравниванию double, между ними четыре пропадавших байта.
/// Размер записи от него не изменился — двадцать четыре байта как было.
///
/// Нужен затем, что без него каждая проба двоичного поиска стоила трёх
/// зависимых загрузок: коробка → таблица → её арена, и только потом сравнение
/// байт. Имена полей почти всегда расходятся в первых четырёх байтах
/// (`id`, `name`, `price`, `label`), так что теперь почти всякая проба
/// решается одним целочисленным сравнением, не трогая таблицу вовсе.
struct Entry {
    std::uint32_t key;
    std::uint32_t prefix;
    Value value;
};

/// An object: header and pairs in one allocation, the same shape ArrayBox
/// uses.
///
///   ObjectBox                             one allocation
///  ┌──────────────────────────────┐
///  │ rc, kind                     │ Box
///  │ epoch                        │      same offset as ArrayBox::epoch
///  │ keys ── KeyTable *           │      held by reference
///  │ len, cap                     │
///  │ data ──────────┐             │
///  ├────────────────▼─────────────┤
///  │ Entry  Entry  Entry  ...     │      tail, cap slots, sorted by key bytes
///  └──────────────────────────────┘
struct ObjectBox : Box {
    /// See ArrayBox::epoch: first member after Box in both aggregates, on
    /// purpose, so epochAddressOf's kind-dispatch answers with one offset.
    Epoch epoch;

    KeyTable *keys;              // удерживается ссылкой
    std::uint32_t len;
    std::uint32_t cap;
    Entry *data;

    [[nodiscard]] std::uint32_t size() const noexcept { return len; }
    [[nodiscard]] const Entry &at(std::uint32_t i) const noexcept {
        assert(i < len);
        return data[i];
    }
    void setValue(std::uint32_t i, Value v) noexcept {
        assert(i < len);
        data[i].value = v;
    }
    /// Inserts a pair at position `at`, keeping the pairs sorted by key bytes.
    void insert(std::uint32_t at, const Entry &entry);
    [[nodiscard]] Entry *tail() noexcept {
        return reinterpret_cast<Entry *>(reinterpret_cast<char *>(this) +
                                         sizeof(ObjectBox));
    }
};

/// Номер пары с этим ключом, а если ключа нет — место, куда её вставить,
/// чтобы порядок сохранился. found получает признак находки.
///
/// Живёт здесь, а не у Store, потому что читает только саму коробку: имена
/// берутся из таблицы **коробки**, а не из таблицы чьего-то хранилища. На этом стоит
/// выдача объекта наружу — прочитать его вправе кто угодно и когда угодно,
/// в том числе когда контекста уже нет.
///
/// Пары упорядочены по **байтам** ключа, а не по номеру в таблице. Порядок
/// перечисления наружу формально не обещан (docs/semantics.md §2.1), но
/// фактически он байтовый, и на нём стоит вывод printValue с золотыми тестами.
std::uint32_t findEntry(const ObjectBox &box, std::string_view key,
                        bool *found) noexcept;

/// То же, но с уже посчитанным префиксом ключа. Для записи: та вставляет
/// префикс в новую пару и иначе считала бы его дважды за один вызов.
std::uint32_t findEntry(const ObjectBox &box, std::string_view key,
                        std::uint32_t prefix, bool *found) noexcept;

/// Счётчик у новорождённого — 1, и эта ссылка принадлежит создателю.
/// birth — номер с ленты контекста: рождение берёт из неё так же, как мутация.
StringBox *makeStringBox(std::string_view bytes);
ArrayBox *makeArrayBox(std::uint32_t capacity, Epoch birth);
/// Ссылку на таблицу коробка берёт сама.
ObjectBox *makeObjectBox(KeyTable *keys, std::uint32_t capacity, Epoch birth);

/// Коробка, которой значение владеет, либо nullptr, если не владеет ничем.
///
/// Один предикат на весь движок. Вопрос «держит ли это значение ссылку»
/// задают четверо — взятие ссылки, отпускание, отложенное освобождение и
/// граница с хостом, — и до этой функции каждый спрашивал своими словами,
/// повторяя одно и то же условие пятью разными способами.
///
/// Отрицательный ответ один: у скаляра ссылаться нечему вовсе.
[[nodiscard]] inline Box *boxOf(Value v) noexcept {
    return v.referencesBox() ? v.box() : nullptr;
}

inline void retain(Box *box) noexcept { ++box->rc; }

/// Отпускает ссылку; на нуле разрушает коробку, рекурсивно отпуская содержимое.
void release(Box *box) noexcept;

/// Отпускает ссылку значения, если оно ею владеет.
inline void releaseValue(Value v) noexcept {
    if (Box *box = boxOf(v)) { release(box); }
}

/// Берёт ссылку на значение, если оно ею владеет.
inline void retainValue(Value v) noexcept {
    if (Box *box = boxOf(v)) { retain(box); }
}

#ifndef NDEBUG
/// Number of boxes alive in this process right now.
///
/// Debug builds only, and that is the whole point: this is a test metric, not
/// a runtime one. Release builds carry neither the counter nor the increments.
/// Every test that reads it must be guarded by #ifndef NDEBUG too.
[[nodiscard]] std::size_t liveBoxCount() noexcept;
#endif

}  // namespace detail

/// Bytes of a string value.
///
/// The sole reading door for a string: no Store is consulted. A short string
/// carries its bytes inside the value itself; a long string is a box
/// (detail::StringBox) that reads itself without asking anyone where it
/// lives. Takes the value by const reference, not by copy — an inline
/// string's bytes live inside the Value, and a by-value parameter would hand
/// back a pointer into a temporary that is already gone.
///
/// Precondition: v.kind() == Value::Kind::String.
/// The returned view is valid exactly as long as v is: until the end of v's
/// scope for an inline string, or until the box's reference count reaches
/// zero for a boxed one.
[[nodiscard]] inline std::string_view stringBytes(const Value &v) noexcept {
    assert(v.kind() == Value::Kind::String);
    if (v.isInlineString()) { return v.inlineBytes(); }
    return static_cast<const detail::StringBox *>(v.box())->view();
}

/// A temporary is not a valid source of bytes: an inline string's bytes live
/// inside the value, and the value would die at the end of the full
/// expression while the slice was still being read. Bind it to a named
/// const Value & instead.
std::string_view stringBytes(Value &&) = delete;

/// Адрес эпохи агрегата — то, что уезжает читателю зависимостью.
///
/// Предусловие: v.kind() — Array либо Object. У скаляра и у строки эпохи нет
/// и быть не может: скаляру не за что зацепиться, а строку в языке нечем
/// изменить (спека §2.8).
[[nodiscard]] inline const Epoch *epochAddressOf(const Value &v) noexcept {
    assert(v.kind() == Value::Kind::Array || v.kind() == Value::Kind::Object);
    // Разбор по виду, а не единый каст в ArrayBox: смещения у обеих коробок
    // совпадают (static_assert в box.cpp это сторожит), но каст объекта к
    // неродственному типу — UB, и UBSan на нём срабатывает. Цена разбора —
    // один предсказуемый переход, и только на пути промаха.
    if (v.kind() == Value::Kind::Array) {
        return &static_cast<const detail::ArrayBox *>(v.box())->epoch;
    }
    return &static_cast<const detail::ObjectBox *>(v.box())->epoch;
}

}  // namespace CS
