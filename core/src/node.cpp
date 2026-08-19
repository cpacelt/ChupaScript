#include "node.hpp"

#include <cassert>
#include <cstring>
#include <new>

#include "keytable.hpp"

namespace CS {
namespace detail {

/// Живые узлы. Без атомарности: контекст однопоточный, как и счётчик самого
/// узла.
static std::size_t g_liveNodes = 0;

std::size_t liveNodeCount() noexcept { return g_liveNodes; }

std::string_view StrNode::view() const noexcept {
    if (len == 0) { return {}; }
    return std::string_view(reinterpret_cast<const char *>(this) + sizeof(StrNode),
                            len);
}

StrNode *makeStrNode(std::string_view bytes) {
    assert(bytes.size() <= 0xffffffffu && "строка переросла uint32");
    // Одна аллокация на заголовок и байты: у строки нет ни роста, ни
    // перезаписи, поэтому отдельный буфер ей был бы только лишней косвенностью.
    void *raw = ::operator new(sizeof(StrNode) + bytes.size());
    StrNode *node = new (raw) StrNode();
    node->rc = 1;
    node->kind = Value::Kind::String;
    node->len = static_cast<std::uint32_t>(bytes.size());
    if (!bytes.empty()) {
        std::memcpy(reinterpret_cast<char *>(raw) + sizeof(StrNode), bytes.data(),
                    bytes.size());
    }
    ++g_liveNodes;
    return node;
}

ArrayNode *makeArrayNode(std::uint32_t capacity) {
    ArrayNode *node = new ArrayNode();
    node->rc = 1;
    node->kind = Value::Kind::Array;
    if (capacity > 0) { node->items.reserve(capacity); }
    ++g_liveNodes;
    return node;
}

ObjectNode *makeObjectNode(KeyTable *keys, std::uint32_t capacity) {
    assert(keys != nullptr);
    ObjectNode *node = new ObjectNode();
    node->rc = 1;
    node->kind = Value::Kind::Object;
    node->keys = keys;
    KeyTable::retain(keys);
    if (capacity > 0) { node->entries.reserve(capacity); }
    ++g_liveNodes;
    return node;
}

std::uint32_t findEntry(const ObjectNode &node, std::string_view key,
                        bool *found) noexcept {
    // Пары отсортированы по байтам ключа, поиск двоичный: на типичных 3–20
    // ключах это дешевле хеш-таблицы и не выделяет ничего сверх самого вектора.
    std::uint32_t low = 0;
    std::uint32_t high = static_cast<std::uint32_t>(node.entries.size());
    while (low < high) {
        const std::uint32_t mid = low + (high - low) / 2;
        const std::string_view candidate = node.keys->bytes(node.entries[mid].key);
        if (candidate < key) {
            low = mid + 1;
        } else if (key < candidate) {
            high = mid;
        } else {
            *found = true;
            return mid;
        }
    }
    *found = false;
    return low;
}

/// Отпустить значение, если оно вообще на что-то ссылается счётчиком.
static void releaseValue(Value v) noexcept {
    if (v.addressesStore() && v.region() == Value::Region::Counted) {
        release(v.node());
    }
}

void release(Node *node) noexcept {
    assert(node != nullptr && node->rc > 0);
    if (--node->rc != 0) { return; }
    --g_liveNodes;

    switch (node->kind) {
        case Value::Kind::String: {
            // Симметрично makeStrNode: место взято у operator new руками,
            // значит и вернуть его надо руками, а деструктор позвать отдельно.
            StrNode *s = static_cast<StrNode *>(node);
            s->~StrNode();
            ::operator delete(static_cast<void *>(s));
            return;
        }
        case Value::Kind::Array: {
            ArrayNode *a = static_cast<ArrayNode *>(node);
            for (Value v : a->items) { releaseValue(v); }
            delete a;
            return;
        }
        case Value::Kind::Object: {
            ObjectNode *o = static_cast<ObjectNode *>(node);
            for (const Entry &e : o->entries) { releaseValue(e.value); }
            KeyTable::release(o->keys);
            delete o;
            return;
        }
        default:
            assert(false && "узла такого вида не бывает");
            return;
    }
}

}  // namespace detail
}  // namespace CS
