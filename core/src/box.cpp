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

namespace {

/// Next capacity for an aggregate that has run out of room. Doubling, with a
/// floor of four: an array grown through push takes elements one at a time,
/// and a floor keeps the first few pushes from reallocating on every step.
std::uint32_t grownCapacity(std::uint32_t cap) noexcept {
    if (cap < 4) { return 4u; }
    // The clamp is not decoration: cap * 2 wraps to zero at 2^31, and a zero
    // capacity would make push write past the end of a zero-sized buffer.
    // An aggregate that large is a different problem, and it trips a debug
    // assertion — compiled out under NDEBUG, same as every assert here.
    assert(cap <= 0x7fffffffu && "aggregate outgrew uint32 capacity");
    return cap * 2u;
}

}  // namespace

ArrayBox *makeArrayBox(std::uint32_t capacity, Epoch birth) {
    void *raw = ::operator new(sizeof(ArrayBox) + capacity * sizeof(Value));
    ArrayBox *box = new (raw) ArrayBox;
    box->rc = 1;
    box->kind = Value::Kind::Array;
    box->epoch = birth;
    box->len = 0;
    box->cap = capacity;
    box->data = box->tail();
    CHUPA_COUNT_BOX_BORN();
    return box;
}

void ArrayBox::push(Value v) {
    if (len == cap) {
        const std::uint32_t grown = grownCapacity(cap);
        Value *moved = static_cast<Value *>(::operator new(grown * sizeof(Value)));
        // Value is trivially copyable, so moving the elements is a copy of the
        // bytes and no reference counts change hands.
        std::memcpy(moved, data, len * sizeof(Value));
        if (data != tail()) { ::operator delete(static_cast<void *>(data)); }
        data = moved;
        cap = grown;
    }
    data[len++] = v;
}

ObjectBox *makeObjectBox(KeyTable *keys, std::uint32_t capacity, Epoch birth) {
    assert(keys != nullptr);
    void *raw = ::operator new(sizeof(ObjectBox) + capacity * sizeof(Entry));
    ObjectBox *box = new (raw) ObjectBox;
    box->rc = 1;
    box->kind = Value::Kind::Object;
    box->epoch = birth;
    box->keys = keys;
    KeyTable::retain(keys);
    box->len = 0;
    box->cap = capacity;
    box->data = box->tail();
    CHUPA_COUNT_BOX_BORN();
    return box;
}

// ArrayBox и ObjectBox не standard-layout формально: и у Box (базы), и у них
// самих есть нестатические поля данных, а стандарт требует, чтобы поля были
// только у одного класса в иерархии. -Winvalid-offsetof предупреждает
// поэтому честно — но раскладка здесь одиночным наследованием без
// виртуальных функций, и оба компилятора (Itanium ABI, MSVC ABI) кладут базу
// первым подобъектом одинаково что для ArrayBox, что для ObjectBox. Смещение
// вычисляется корректно; предупреждение снимается точечно, а не глобально.
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
static_assert(offsetof(detail::ArrayBox, epoch) ==
                  offsetof(detail::ObjectBox, epoch),
              "epochAddressOf читает эпоху одним смещением на оба вида");
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

void ObjectBox::insert(std::uint32_t at, const Entry &entry) {
    assert(at <= len);
    if (len == cap) {
        const std::uint32_t grown = grownCapacity(cap);
        Entry *moved = static_cast<Entry *>(::operator new(grown * sizeof(Entry)));
        std::memcpy(moved, data, len * sizeof(Entry));
        if (data != tail()) { ::operator delete(static_cast<void *>(data)); }
        data = moved;
        cap = grown;
    }
    // memmove, not memcpy: the source and destination ranges overlap by
    // everything but one slot.
    std::memmove(data + at + 1, data + at, (len - at) * sizeof(Entry));
    data[at] = entry;
    ++len;
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
    std::uint32_t high = box.size();
    while (low < high) {
        const std::uint32_t mid = low + (high - low) / 2;
        const Entry &entry = box.at(mid);
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
            for (std::uint32_t i = 0; i < a->len; ++i) { releaseValue(a->data[i]); }
            if (a->data != a->tail()) {
                ::operator delete(static_cast<void *>(a->data));
            }
            a->~ArrayBox();
            ::operator delete(static_cast<void *>(a));
            return;
        }
        case Value::Kind::Object: {
            ObjectBox *o = static_cast<ObjectBox *>(box);
            for (std::uint32_t i = 0; i < o->len; ++i) { releaseValue(o->data[i].value); }
            KeyTable::release(o->keys);
            if (o->data != o->tail()) {
                ::operator delete(static_cast<void *>(o->data));
            }
            o->~ObjectBox();
            ::operator delete(static_cast<void *>(o));
            return;
        }
        default:
            assert(false && "коробки такого вида не бывает");
            return;
    }
}

}  // namespace detail
}  // namespace CS
