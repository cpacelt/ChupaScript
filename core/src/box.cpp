#include "box.hpp"

#include <atomic>
#include <cassert>
#include <cstring>
#include <new>

#include "keytable.hpp"

namespace CS {
namespace detail {

#ifndef NDEBUG
/// Number of live boxes in this process. Debug builds only: a leaked box is
/// invisible to every other metric, because box memory belongs to no Store and
/// Store::bytesUsed cannot see it.
///
/// Atomic because two Contexts may run on two threads (chupascript.h,
/// threading contract), and this counter is the one piece of box state that is
/// process-wide rather than per-Context. Relaxed ordering is enough: the count
/// is read after all evaluation has stopped, never to synchronise one thread's
/// writes with another's reads.
std::atomic<std::size_t> g_liveBoxes{0};

std::size_t liveBoxCount() noexcept {
    return g_liveBoxes.load(std::memory_order_relaxed);
}
#endif

#ifndef NDEBUG
#  define CHUPA_COUNT_BOX_BORN() \
       g_liveBoxes.fetch_add(1, std::memory_order_relaxed)
#  define CHUPA_COUNT_BOX_DIED() \
       g_liveBoxes.fetch_sub(1, std::memory_order_relaxed)
#else
#  define CHUPA_COUNT_BOX_BORN() ((void)0)
#  define CHUPA_COUNT_BOX_DIED() ((void)0)
#endif

std::string_view StringBox::view() const noexcept {
    if (len == 0) { return {}; }
    return std::string_view(reinterpret_cast<const char *>(this) + sizeof(StringBox),
                            len);
}

StringBox *makeStringBox(std::string_view bytes) {
    assert(bytes.size() <= 0xffffffffu && "строка переросла uint32");
    // Одна аллокация на заголовок и байты: у строки нет ни роста, ни
    // перезаписи, поэтому отдельный буфер ей был бы только лишней косвенностью.
    void *raw = ::operator new(sizeof(StringBox) + bytes.size());
    // Без скобок: StringBox() занулил бы rc, kind и len ровно перед тем, как
    // все три перезаписать следующими строками.
    StringBox *box = new (raw) StringBox;
    box->rc = 1;
    box->kind = Value::Kind::String;
    box->len = static_cast<std::uint32_t>(bytes.size());
    if (!bytes.empty()) {
        std::memcpy(reinterpret_cast<char *>(raw) + sizeof(StringBox), bytes.data(),
                    bytes.size());
    }
    CHUPA_COUNT_BOX_BORN();
    return box;
}

ArrayBox *makeArrayBox(std::uint32_t capacity) {
    ArrayBox *box = new ArrayBox();
    box->rc = 1;
    box->kind = Value::Kind::Array;
    if (capacity > 0) { box->items.reserve(capacity); }
    CHUPA_COUNT_BOX_BORN();
    return box;
}

ObjectBox *makeObjectBox(KeyTable *keys, std::uint32_t capacity) {
    assert(keys != nullptr);
    ObjectBox *box = new ObjectBox();
    box->rc = 1;
    box->kind = Value::Kind::Object;
    box->keys = keys;
    KeyTable::retain(keys);
    if (capacity > 0) { box->entries.reserve(capacity); }
    CHUPA_COUNT_BOX_BORN();
    return box;
}

std::uint32_t keyPrefix(std::string_view key) noexcept {
    std::uint32_t out = 0;
    const std::size_t n = key.size() < 4 ? key.size() : 4;
    for (std::size_t i = 0; i < n; ++i) {
        out |= static_cast<std::uint32_t>(static_cast<unsigned char>(key[i]))
               << ((3 - i) * 8);
    }
    return out;
}

std::uint32_t findEntry(const ObjectBox &box, std::string_view key,
                        bool *found) noexcept {
    return findEntry(box, key, keyPrefix(key), found);
}

std::uint32_t findEntry(const ObjectBox &box, std::string_view key,
                        std::uint32_t want, bool *found) noexcept {
    // Пары отсортированы по байтам ключа, поиск двоичный: на типичных 3–20
    // ключах это дешевле хеш-таблицы и не выделяет ничего сверх самого вектора.
    //
    // Сравнение идёт сперва по префиксу — четырём байтам, лежащим в самой
    // записи. Порядок префиксов совпадает с байтовым (см. keyPrefix), поэтому
    // различие префиксов решает пробу целиком, и таблица имён при этом не
    // читается. До байт дело доходит лишь когда первые четыре совпали.
    assert(want == keyPrefix(key));
    std::uint32_t low = 0;
    std::uint32_t high = static_cast<std::uint32_t>(box.entries.size());
    while (low < high) {
        const std::uint32_t mid = low + (high - low) / 2;
        const Entry &entry = box.entries[mid];
        if (entry.prefix != want) {
            if (entry.prefix < want) { low = mid + 1; } else { high = mid; }
            continue;
        }
        const std::string_view candidate = box.keys->bytes(entry.key);
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

void release(Box *box) noexcept {
    assert(box != nullptr && box->rc > 0);
    if (--box->rc != 0) { return; }
    CHUPA_COUNT_BOX_DIED();

    switch (box->kind) {
        case Value::Kind::String: {
            // Симметрично makeStringBox: место взято у operator new руками,
            // значит и вернуть его надо руками, а деструктор позвать отдельно.
            StringBox *s = static_cast<StringBox *>(box);
            s->~StringBox();
            ::operator delete(static_cast<void *>(s));
            return;
        }
        case Value::Kind::Array: {
            ArrayBox *a = static_cast<ArrayBox *>(box);
            for (Value v : a->items) { releaseValue(v); }
            delete a;
            return;
        }
        case Value::Kind::Object: {
            ObjectBox *o = static_cast<ObjectBox *>(box);
            for (const Entry &e : o->entries) { releaseValue(e.value); }
            KeyTable::release(o->keys);
            delete o;
            return;
        }
        default:
            assert(false && "коробки такого вида не бывает");
            return;
    }
}

}  // namespace detail
}  // namespace CS
