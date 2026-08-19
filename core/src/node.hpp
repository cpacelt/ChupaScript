#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "value.hpp"

namespace CS {

class KeyTable;

namespace detail {

/// Заголовок всякого узла со счётчиком ссылок.
///
/// Счётчик интрузивный, а не shared_ptr, и причин тому четыре: тот
/// шестнадцать байт и в объединение Value (восемь) не влезает; ломает
/// тривиальную копируемость, на которой стоит static_assert; требует второй
/// аллокации под блок управления, а строке нужна одна, хвостом; считает
/// атомарно, тогда как контекст однопоточный.
///
/// Обоснование: docs/superpowers/specs/2026-08-19-chupascript-memory-model-design.md Р3.
struct Node {
    std::uint32_t rc;
    Value::Kind kind;
};

/// Строка: заголовок и байты одной аллокацией, байты сразу за заголовком.
///
/// Члена под байты нет: массив переменной длины — расширение, а не стандарт.
/// Добираться до них надо через view(), больше ниоткуда они не видны.
struct StrNode : Node {
    std::uint32_t len;

    [[nodiscard]] std::string_view view() const noexcept;
};

struct ArrayNode : Node {
    std::vector<Value> items;
};

/// Пара объекта: номер ключа в таблице интернирования и значение.
struct Entry {
    std::uint32_t key;
    Value value;
};

struct ObjectNode : Node {
    KeyTable *keys;              // удерживается ссылкой
    std::vector<Entry> entries;  // упорядочены по байтам ключа
};

/// Счётчик у новорождённого — 1, и эта ссылка принадлежит создателю.
StrNode *makeStrNode(std::string_view bytes);
ArrayNode *makeArrayNode(std::uint32_t capacity);
/// Ссылку на таблицу узел берёт сам.
ObjectNode *makeObjectNode(KeyTable *keys, std::uint32_t capacity);

inline void retain(Node *node) noexcept { ++node->rc; }

/// Отпускает ссылку; на нуле разрушает узел, рекурсивно отпуская содержимое.
void release(Node *node) noexcept;

/// Сколько узлов сейчас живо во всём процессе.
///
/// Метрика для тестов, и другой у нас нет: память узла хранилищу не
/// принадлежит, поэтому Store::bytesUsed её не видит и утечку ею не поймать.
std::size_t liveNodeCount() noexcept;

}  // namespace detail
}  // namespace CS
