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

    // ─── чтение ───

    /// Предусловие: v.kind() == Value::Kind::String.
    std::string_view string(Value v) const noexcept;

    /// Предусловие: a.kind() == Value::Kind::Array.
    std::uint32_t arrayCount(Value a) const noexcept;

    /// Элемент либо null за границей (docs/semantics.md §6.1).
    /// Предусловие: a.kind() == Value::Kind::Array.
    Value arrayAt(Value a, std::uint32_t index) const noexcept;

    // ─── изменение ───

    /// Заменяет элемент. false за границей — по docs/semantics.md §7.2 это
    /// ошибка, диагностику формулирует вызывающий.
    /// Предусловие: a.kind() == Value::Kind::Array.
    bool arraySet(Value a, std::uint32_t index, Value v) noexcept;

    // ─── метрики ───

    /// Сколько байт занято выданными данными.
    std::size_t bytesUsed() const noexcept;
    /// Сколько байт занято у аллокатора, включая запас пулов.
    std::size_t bytesReserved() const noexcept;

   private:
    std::uint32_t appendText(std::string_view bytes);
    std::string_view textAt(std::uint32_t offset, std::uint32_t length) const noexcept;
    void growArray(detail::ArrayRep &rep, std::uint32_t needed);

    std::vector<Value> pool_;                   // элементы массивов, диапазонами
    std::vector<detail::ArrayRep> arrays_;      // заголовки массивов
    std::vector<detail::ObjectRep> objects_;    // заголовки объектов
    std::vector<detail::Entry> entries_;        // пары объектов, диапазонами
    std::vector<char> text_;                    // байты строк и ключей
};

}  // namespace CS
