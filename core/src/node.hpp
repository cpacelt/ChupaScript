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

/// Номер пары с этим ключом, а если ключа нет — место, куда её вставить,
/// чтобы порядок сохранился. found получает признак находки.
///
/// Живёт здесь, а не у Store, потому что читает только сам узел: имена берутся
/// из таблицы **узла**, а не из таблицы чьего-то хранилища. На этом стоит
/// выдача объекта наружу — прочитать его вправе кто угодно и когда угодно,
/// в том числе когда контекста уже нет.
///
/// Пары упорядочены по **байтам** ключа, а не по номеру в таблице. Порядок
/// перечисления наружу формально не обещан (docs/semantics.md §2.1), но
/// фактически он байтовый, и на нём стоит вывод printValue с золотыми тестами.
std::uint32_t findEntry(const ObjectNode &node, std::string_view key,
                        bool *found) noexcept;

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
