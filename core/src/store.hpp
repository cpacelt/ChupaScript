#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "keytable.hpp"
#include "box.hpp"
#include "deferred.hpp"
#include "value.hpp"

namespace CS {

namespace detail {
/// Запись таблицы имён. Определена только в store.cpp: снаружи это неполный
/// тип, и раскладку таблицы не видит никто.
struct GlobalName;
}  // namespace detail

/// The Store the host talks to: names and values of global variables.
///
///   names_   bytes of global variable names, append-only
///   ┌──────────────────────────────────────┐
///   │ "width" "alpha" "user" ...            │
///   └──────────────────────────────────────┘
///        ▲        ▲
///        │        │  slots_: name → slot, sorted by name
///   ┌────┴────┬────┴────┬─────────────┐
///   │ "alpha" │ "width" │ "user"  ...  │  (each entry: bytes offset+len, slot)
///   └────┬────┴────┬────┴──────┬──────┘
///        │         │           │
///        ▼         ▼           ▼
///   ┌─────────┬─────────┬──────────────┐
///   │  values_: slot → Value, append-only, slots never move                │
///   └─────────────────────────────────────────────────────────────────────┘
///
///   keys_  → KeyTable (field-name interning), owned, released in ~Store
///   id_    → this Store's identity, checked by Execution::acceptsUnit
///
/// A String, Object or Array Value is a box (box.hpp) and reads itself
/// without any Store; the Store never owns one — it only creates them
/// (through CS::setVariable / CS::materialize) and hands the creator's
/// reference to the caller's Deferred list. There used to be a second role
/// here, an "arena" of temporary bytes shared by every string a running
/// expression produced; that role is gone along with the offset-addressed
/// strings it served — every string is a self-contained box now, so there is
/// nothing left for an arena to hold.
///
/// Срез, который этот класс отдаёт наружу (`globalNameAt()`), переживает лишь
/// до ближайшей мутации того, откуда он взят.
///
/// Обоснование:
/// docs/superpowers/specs/2026-08-19-chupascript-memory-model-design.md
class Store {
   public:
    Store();

    /// Определён в store.cpp: в заголовке типы пулов ещё неполны.
    ~Store();

    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    // ─── создание ───

    // Строка коробкой сюда не входит: CS::materialize (core/src/aggregate.hpp)
    // не читает ни одного члена хранилища, ей нужен только список
    // отложенного освобождения.

    // Создание агрегата сюда не входит: коробке хранилище не нужно ни для
    // чего, кроме списка отложенного освобождения (core/src/aggregate.hpp).

    // Building a string piece by piece is not here either: format
    // (docs/semantics.md §8.8) assembles it in its own buffer, owned by the
    // execution — Execution::builder_ (core/src/execution.hpp) — not by the
    // store.

    // Список отложенного освобождения сюда не входит. Он принадлежит
    // выполнению (Execution::deferred), а не хранилищу: сливает его граница
    // операции, а хранилищ по обе стороны от той было два на один список.

    /// Таблица имён полей этого хранилища.
    [[nodiscard]] KeyTable *keys() const noexcept { return keys_; }

    /// Identity of this Store, unique among all Stores created in this
    /// process.
    ///
    /// A compiled unit records the id of the Store it was compiled against and
    /// refuses to run on any other one (Expression::eval). Without it a unit
    /// evaluated on a foreign Context would index that Context's values_ with a
    /// slot number THIS Store handed out, and return whichever variable happens
    /// to sit there.
    ///
    /// A number rather than the Store's address: an address is reused the
    /// moment one Store is destroyed and the next is allocated in its place,
    /// and a unit outliving its Context is exactly the case this check exists
    /// for.
    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }

    // ─── чтение ───

    // Чтение строки сюда не входит и входить не может: строка — box.hpp
    // StringBox — самодостаточна и читается через свободную функцию
    // CS::stringBytes, не спрашивая никакое хранилище.

    // Чтение агрегата сюда не входит и входить не может: агрегат
    // самодостаточен, и ни один член хранилища при чтении не участвует
    // (core/src/aggregate.hpp).

    // Изменение агрегата сюда не входит по той же причине, что и чтение:
    // мутатору нужен только сам агрегат и список отложенного освобождения
    // (core/src/aggregate.hpp).

    // ─── глобальные переменные ───
    //
    // Раньше таблица глобальных переменных была обычным объектом: имя → значение,
    // и все чтения шли тем же двоичным поиском, что у любого объекта языка.
    // Теперь поиск и хранение разделены.
    //
    // slots_ — имя → номер ячейки, отсортирован по имени. Нужен ровно
    // там, где снаружи приходит имя: компиляции (имя стоит в тексте выражения)
    // и записи (имя приходит от хоста). Оба случая разовые.
    //
    // values_ — номер ячейки → значение, только растёт. Ячейки не
    // переезжают, поэтому номер, разрешённый на компиляции, годен навсегда;
    // вставка нового имени двигает записи slots_, но номера внутри них
    // едут вместе с именами и не меняются.
    //
    // Ради этого разделения и затевалось: вычисление знает номер из узла дерева
    // и берёт значение индексацией, не трогая ни имени, ни поиска. Замер до
    // правки: чтение переменной 37–45 нс против 9 нс у константы, и вся разница
    // была поиском (docs/benchmarks/).

    /// Значение глобальной переменной либо null, если имени нет.
    Value global(std::string_view name) const noexcept;

    /// Есть ли такое имя. Отличает глобальную переменную со значением null
    /// от отсутствующей.
    bool hasGlobal(std::string_view name) const noexcept;

    /// Номер ячейки по имени либо kNoGlobalSlot, если имени нет.
    ///
    /// Зовёт это check (core/src/check.hpp), раз на узел при компиляции, и
    /// кладёт номер в дерево. Больше номер разрешать негде и незачем.
    [[nodiscard]] GlobalSlot globalSlot(std::string_view name) const noexcept;

    /// Значение по номеру ячейки.
    ///
    /// Предусловие: номер выдан **этим** хранилищем. Выражение, вычисляемое на
    /// чужом контексте, прочитало бы чужую ячейку молча, поэтому в отладочной
    /// сборке несовпадение ловится утверждением — как и отметка прохода check
    /// в eval.
    [[nodiscard]] Value globalValueAt(GlobalSlot slot) const noexcept;

    /// Заводит глобальную переменную либо заменяет значение существующей.
    /// Имя не проверяется: требование «всякая глобальная переменная адресуема
    /// идентификатором» держится на вызывающем, и единственный такой
    /// вызывающий — setVariable из data.hpp.
    ///
    /// dead принимает вытесненное значение: ячейка — корень, и без отпускания
    /// прежнего повторная запись растила бы память вечно. Отпускается оно не
    /// на месте, а на границе операции — вызывающий вправе ещё держать копию
    /// того Value, что сейчас вытесняется.
    void setGlobal(std::string_view name, Value v, Deferred &dead);

    std::uint32_t globalCount() const noexcept;

    /// Имя глобальной переменной по порядковому номеру **в алфавитном порядке
    /// имён**, а не по номеру ячейки; пустой срез за границей.
    std::string_view globalNameAt(std::uint32_t i) const noexcept;

    // ─── метрики ───

    /// How many bytes the global variable names occupy.
    std::size_t bytesUsed() const noexcept;
    /// How many bytes the allocator holds, including the pool's spare capacity.
    std::size_t bytesReserved() const noexcept;

   private:
    std::uint32_t appendName(std::string_view bytes);
    std::string_view nameAt(std::uint32_t offset, std::uint32_t length) const noexcept;

    std::uint32_t findGlobal(std::string_view name, bool *found) const noexcept;

    // ─── байты ───
    //
    // names_ is the sole address space for global variable names: a name
    // handed to setGlobal is copied here and looked up as an (offset,
    // length) pair. Nothing else lands here — a string literal's bytes live
    // in the Ast, an object key lives in the KeyTable, and the result of str
    // or format lives in a box. Append-only, no truncation, so two equal
    // names occupy two copies (docs/backlog.md B1, B51).
    std::vector<char> names_;  // bytes of global variable names

    /// Таблица имён полей контекста. Владеет ею это хранилище и только оно.
    ///
    /// Выдаётся наружу через keys() тем, кто создаёт объект: коробка удержит
    /// её сама. Чтение чужого объекта сюда не смотрит — тот носит свою.
    ///
    /// Удерживается ссылкой: её переживает всякий объект, уехавший к хосту,
    /// поэтому умереть вместе с хранилищем она не вправе.
    KeyTable *keys_;

    const std::uint32_t id_;

    // ─── глобальные переменные: поиск отдельно от хранения ───
    //
    // slots_ отсортирован по имени и читается только там, где имя
    // приходит снаружи: на компиляции (check разрешает имя в номер ячейки и
    // кладёт номер в узел дерева) и на записи от хоста. Вычисление сюда не
    // ходит вовсе — оно берёт значение индексацией values_ по номеру из
    // узла.
    //
    // Значение лежит в ячейке, а не в записи имени, именно ради этого: вставка
    // нового имени сдвигает записи, чтобы сохранить сортировку, и значение
    // сдвинулось бы вместе с записью, обесценив номер, разрешённый на
    // компиляции. values_ только растёт, ячейки не переезжают.
    //
    // Первый setGlobal("width", 320) заводит ячейку 0 со значением 320 и
    // запись {смещение байт `width` в names_, 5, 0}. Повторный setGlobal того
    // же имени перезаписывает values_[0] и в slots_ не пишет ничего.
    // setGlobal("alpha", …) добавляет ячейку 1, но её запись встаёт в таблице
    // имён перед `width`: номер ячейки от места в таблице не зависит.
    std::vector<detail::GlobalName> slots_;  // имя → номер ячейки
    std::vector<Value> values_;              // номер ячейки → значение
};

}  // namespace CS
