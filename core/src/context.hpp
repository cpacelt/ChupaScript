#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "value.hpp"

namespace CS {

namespace detail {
/// Заголовки агрегатов и запись объекта. Определены только в context.cpp:
/// снаружи это неполные типы, и раскладку хранилища не видит никто.
struct ArrayRep;
struct ObjectRep;
struct Entry;
}  // namespace detail

/// Хранилище значений ChupaScript.
///
/// Владеет всем, что породил; поштучного освобождения нет, вся память уходит
/// разом в деструкторе. Значения адресуют пулы индексами, поэтому пулы вправе
/// переезжать при росте — а вот указатель на элемент пула переживает лишь до
/// ближайшей мутации, и наружу такие указатели этот класс не отдаёт.
///
/// Обоснование раскладки:
/// docs/superpowers/specs/2026-08-11-chupascript-values-design.md §5–§7.
class Context {
   public:
    Context();
    /// Определён в context.cpp: в заголовке типы пулов ещё неполны.
    ~Context();

    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;

    // ─── создание ───

    /// Копирует байты в пул текста. Допускает срез собственного пула.
    Value makeString(std::string_view bytes);

    /// Создаёт пустой массив. capacity — сколько элементов выделить заранее;
    /// на длину не влияет, элементы добавляет только arrayPush.
    Value makeArray(std::uint32_t capacity = 0);

    /// Создаёт пустой объект. capacity — сколько пар выделить заранее.
    Value makeObject(std::uint32_t capacity = 0);

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

    // ─── метрики ───

    /// Сколько байт занято выданными данными.
    std::size_t bytesUsed() const noexcept;
    /// Сколько байт занято у аллокатора, включая запас пулов.
    std::size_t bytesReserved() const noexcept;

   private:
    std::uint32_t appendText(std::string_view bytes);
    std::string_view textAt(std::uint32_t offset, std::uint32_t length) const noexcept;
    void growArray(detail::ArrayRep &rep, std::uint32_t needed);

    void growObject(detail::ObjectRep &rep, std::uint32_t needed);

    /// Номер ключа, а если ключа нет — место, куда его вставить, чтобы
    /// сортировка сохранилась. found получает признак находки.
    std::uint32_t findKey(const detail::ObjectRep &rep, std::string_view key,
                          bool *found) const noexcept;

    std::vector<Value> pool_;                   // элементы массивов, диапазонами
    std::vector<detail::ArrayRep> arrays_;      // заголовки массивов
    std::vector<detail::ObjectRep> objects_;    // заголовки объектов
    std::vector<detail::Entry> entries_;        // пары объектов, диапазонами
    std::vector<char> text_;                    // байты строк и ключей
};

}  // namespace CS
