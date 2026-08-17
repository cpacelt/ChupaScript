#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "value.hpp"

namespace CS {

namespace detail {
/// Заголовки агрегатов и запись объекта. Определены только в store.cpp:
/// снаружи это неполные типы, и раскладку хранилища не видит никто.
struct ArrayRep;
struct ObjectRep;
struct Entry;
struct GlobalName;
}  // namespace detail

/// Хранилище значений ChupaScript.
///
/// Владеет всем, что породил; поштучного освобождения нет, вся память уходит
/// разом в деструкторе. Значения адресуют пулы индексами, поэтому пулы вправе
/// переезжать при росте. Указатель или срез внутрь пула, который этот класс
/// всё же отдаёт наружу — `string()`, `objectKeyAt()` — переживает лишь до
/// ближайшей мутации того же хранилища; дольше их хранить нельзя.
///
/// Обоснование раскладки:
/// docs/superpowers/specs/2026-08-11-chupascript-values-design.md §5–§7.
class Store {
   public:
    Store();
    /// Определён в store.cpp: в заголовке типы пулов ещё неполны.
    ~Store();

    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    // ─── создание ───

    /// Копирует байты в пул текста. Допускает срез собственного пула.
    Value makeString(std::string_view bytes);

    /// Создаёт пустой массив. capacity — сколько элементов выделить заранее;
    /// на длину не влияет, элементы добавляет только arrayPush.
    Value makeArray(std::uint32_t capacity = 0);

    /// Создаёт пустой объект. capacity — сколько пар выделить заранее.
    Value makeObject(std::uint32_t capacity = 0);

    // ─── сборка строки по частям ───
    //
    // Нужна format (docs/semantics.md §8.8): длина результата заранее
    // неизвестна, а требовать её вторым проходом значило бы представлять числа
    // дважды. Сборка идёт в собственном буфере build_, отдельном от пула
    // текста text_, а не в самом text_. Причина — не переезд как таковой (с
    // ним переживший переезд срез был бы в порядке), а то, что вычисление
    // очередного куска (аргумент format — строковый литерал, str, вложенный
    // format) само пишет в text_ и не спрашивает разрешения.
    // Если бы сборка велась в text_, чужая запись легла бы прямо между уже
    // собранными кусками и endString забрал бы её как часть результата —
    // сборка не умеет отличить свою запись от чужой, если обе в одном месте.
    // Раздельные хранилища снимают вопрос целиком: что бы ни писало в text_
    // между beginString и endString, build_ этого не видит.
    //
    // Тем же свойством вложенная сборка становится безопасной сама по себе:
    // внутренний format получает мету — позицию в build_ выше меты внешнего,
    // достраивает свой хвост и снимает его целиком через endString раньше,
    // чем внешний допишет следующий кусок. Это стек, а не разделяемый пул, и
    // рекурсивная сборка на этом свойстве и держится.
    //
    // Метка — позиция в build_, а не указатель: как и с любым vector-подобным
    // буфером, рост может переселить данные, и держаться за смещение, а не за
    // адрес, здесь по-прежнему обязательно.

    /// Начинает сборку. Возвращает метку для endString либо abortString.
    std::uint32_t beginString() noexcept;

    /// Дописывает кусок к собираемой строке.
    void appendToString(std::string_view bytes);

    /// Завершает сборку: копирует собранное — всё, что дописано после метки —
    /// в пул текста и возвращает получившуюся строку. Сам build_ усекается
    /// обратно к метке: собранный хвост в нём больше не нужен.
    Value endString(std::uint32_t mark) noexcept;

    /// Отменяет сборку, усекая build_ к метке.
    void abortString(std::uint32_t mark) noexcept;

    // ─── чтение ───

    /// Предусловие: v.kind() == Value::Kind::String.
    std::string_view string(Value v) const noexcept;

    /// Предусловие: a.kind() == Value::Kind::Array.
    std::uint32_t arrayCount(Value a) const noexcept;

    /// Элемент либо null за границей (docs/semantics.md §6.1).
    /// Предусловие: a.kind() == Value::Kind::Array.
    Value arrayAt(Value a, std::uint32_t index) const noexcept;

    /// Предусловие: o.kind() == Value::Kind::Object.
    std::uint32_t objectCount(Value o) const noexcept;

    /// Значение либо null, если ключа нет (docs/semantics.md §6.2).
    /// Предусловие: o.kind() == Value::Kind::Object.
    Value objectGet(Value o, std::string_view key) const noexcept;

    /// Есть ли ключ. Отличает записанный null от отсутствия — иначе их не
    /// различить (docs/semantics.md §6.2, §8.3).
    /// Предусловие: o.kind() == Value::Kind::Object.
    bool objectHas(Value o, std::string_view key) const noexcept;

    /// Ключ по порядковому номеру либо пустой срез за границей. Порядок
    /// перечисления наружу не обещан (docs/semantics.md §2.1).
    /// Предусловие: o.kind() == Value::Kind::Object.
    std::string_view objectKeyAt(Value o, std::uint32_t i) const noexcept;

    /// Значение по порядковому номеру либо null за границей.
    /// Предусловие: o.kind() == Value::Kind::Object.
    Value objectValueAt(Value o, std::uint32_t i) const noexcept;

    // ─── изменение ───

    /// Заменяет элемент. false за границей — по docs/semantics.md §7.2 это
    /// ошибка, диагностику формулирует вызывающий.
    /// Предусловие: a.kind() == Value::Kind::Array.
    bool arraySet(Value a, std::uint32_t index, Value v) noexcept;

    /// Добавляет элемент в конец. Единственный способ расширить массив
    /// (docs/semantics.md §6.1).
    /// Предусловие: a.kind() == Value::Kind::Array.
    void arrayPush(Value a, Value v);

    /// Снимает последний элемент в *out. false на пустом массиве; выходной
    /// параметр при отказе не меняется. out допускает nullptr.
    /// Предусловие: a.kind() == Value::Kind::Array.
    bool arrayPop(Value a, Value *out) noexcept;

    /// Записывает значение по ключу: заменяет существующее либо создаёт ключ
    /// (docs/semantics.md §6.2). Байты нового ключа копируются в пул текста.
    /// Предусловие: o.kind() == Value::Kind::Object.
    void objectSet(Value o, std::string_view key, Value v);

    // ─── глобальные переменные ───
    //
    // Раньше таблица глобальных переменных была обычным объектом: имя → значение,
    // и все чтения шли тем же двоичным поиском, что у любого объекта языка.
    // Теперь поиск и хранение разделены.
    //
    // globalNames_ — имя → номер ячейки, отсортирован по имени. Нужен ровно
    // там, где снаружи приходит имя: компиляции (имя стоит в тексте выражения)
    // и записи (имя приходит от хоста). Оба случая разовые.
    //
    // globalValues_ — номер ячейки → значение, только растёт. Ячейки не
    // переезжают, поэтому номер, разрешённый на компиляции, годен навсегда;
    // вставка нового имени двигает записи globalNames_, но номера внутри них
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
    void setGlobal(std::string_view name, Value v);

    std::uint32_t globalCount() const noexcept;

    /// Имя глобальной переменной по порядковому номеру **в алфавитном порядке
    /// имён**, а не по номеру ячейки; пустой срез за границей.
    std::string_view globalNameAt(std::uint32_t i) const noexcept;

    // ─── метрики ───

    /// Сколько байт занято выданными данными. Черновик сборки build_ сюда не
    /// входит: пока endString не скопировал его в text_, это ничей черновик, а
    /// не выданные данные — на него не указывает ни одно Value.
    std::size_t bytesUsed() const noexcept;
    /// Сколько байт занято у аллокатора, включая запас пулов — в том числе
    /// build_: endString возвращает его к нулевой длине, но не к нулевой
    /// ёмкости, и эта ёмкость — тот же самый резерв, что и у прочих пулов.
    std::size_t bytesReserved() const noexcept;

   private:
    std::uint32_t appendText(std::string_view bytes);
    std::string_view textAt(std::uint32_t offset, std::uint32_t length) const noexcept;
    /// exact — выделить ровно needed, а не ближайшую степень двойки. Так
    /// зовут makeArray, у которого длина известна заранее и удваивать нечего;
    /// arrayPush растит без exact, чтобы не переезжать на каждом элементе.
    void growArray(detail::ArrayRep &rep, std::uint32_t needed, bool exact = false);

    /// exact — тот же смысл, что и у growArray.
    void growObject(detail::ObjectRep &rep, std::uint32_t needed, bool exact = false);

    /// Номер ключа, а если ключа нет — место, куда его вставить, чтобы
    /// сортировка сохранилась. found получает признак находки.
    std::uint32_t findKey(const detail::ObjectRep &rep, std::string_view key,
                          bool *found) const noexcept;

    /// То же для таблицы имён: номер записи в globalNames_, а если имени нет —
    /// место вставки. Отдельно от findKey, потому что ищет в другом массиве:
    /// у глобальных имён свои записи, а не Entry общего объекта.
    std::uint32_t findGlobal(std::string_view name, bool *found) const noexcept;

    std::vector<Value> pool_;                   // элементы массивов, диапазонами
    std::vector<detail::ArrayRep> arrays_;      // заголовки массивов
    std::vector<detail::ObjectRep> objects_;    // заголовки объектов
    std::vector<detail::Entry> entries_;        // пары объектов, диапазонами
    std::vector<char> text_;                    // байты строк и ключей
    std::string build_;                         // черновик сборки format, отдельно от text_

    std::vector<detail::GlobalName> globalNames_;  // имя → ячейка, по имени
    std::vector<Value> globalValues_;              // ячейка → значение, только растёт
};

}  // namespace CS
