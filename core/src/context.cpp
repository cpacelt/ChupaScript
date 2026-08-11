#include "context.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace CS {

namespace detail {

struct ArrayRep {
    std::uint32_t start;     // индекс первого элемента в pool_
    std::uint32_t count;
    std::uint32_t capacity;
};

struct Entry {
    std::uint32_t keyOffset;  // индекс первого байта ключа в text_
    std::uint32_t keyLength;
    Value value;
};

struct ObjectRep {
    std::uint32_t start;     // индекс первой пары в entries_
    std::uint32_t count;
    std::uint32_t capacity;
};

}  // namespace detail

Context::Context() = default;
Context::~Context() = default;

std::uint32_t Context::appendText(std::string_view bytes) {
    const std::uint32_t offset = static_cast<std::uint32_t>(text_.size());
    assert(text_.size() + bytes.size() <= 0xffffffffu && "пул текста перерос uint32");

    // bytes вправе указывать внутрь text_ — так выглядит objectSet(o,
    // ctx.string(k), v). Рост пула переселит буфер, и указатель источника
    // повиснет прямо посреди копирования, поэтому положение источника
    // запоминается смещением, а не адресом.
    const char *first = text_.data();
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(first);
    const std::uintptr_t from = reinterpret_cast<std::uintptr_t>(bytes.data());
    const bool aliases = first != nullptr && from >= base && from <= base + text_.size();
    const std::size_t inner = aliases ? static_cast<std::size_t>(from - base) : 0;

    text_.resize(text_.size() + bytes.size());
    if (bytes.empty()) { return offset; }

    const char *source = aliases ? text_.data() + inner : bytes.data();
    std::memcpy(text_.data() + offset, source, bytes.size());
    return offset;
}

std::string_view Context::textAt(std::uint32_t offset,
                                 std::uint32_t length) const noexcept {
    if (length == 0) { return {}; }
    return std::string_view(text_.data() + offset, length);
}

Value Context::makeString(std::string_view bytes) {
    const std::uint32_t offset = appendText(bytes);
    return Value::string(offset, static_cast<std::uint32_t>(bytes.size()));
}

std::string_view Context::string(Value v) const noexcept {
    assert(v.kind() == Value::Kind::String);
    return textAt(v.index(), v.length());
}

void Context::growArray(detail::ArrayRep &rep, std::uint32_t needed) {
    if (needed <= rep.capacity) { return; }

    std::uint32_t capacity = rep.capacity == 0 ? 4 : rep.capacity;
    while (capacity < needed) {
        assert(capacity <= 0x7fffffffu && "массив перерос uint32");
        capacity *= 2;
    }

    // Новый диапазон дописывается в хвост, старый бросается мусором:
    // освобождения по одному нет (спека §5). Индекс заголовка при этом не
    // меняется — на нём стоит идентичность и все алиасы.
    const std::uint32_t start = static_cast<std::uint32_t>(pool_.size());
    pool_.insert(pool_.end(), capacity, Value::null());
    for (std::uint32_t i = 0; i < rep.count; ++i) {
        pool_[start + i] = pool_[rep.start + i];
    }

    rep.start = start;
    rep.capacity = capacity;
}

Value Context::makeArray(std::uint32_t capacity) {
    const std::uint32_t index = static_cast<std::uint32_t>(arrays_.size());
    arrays_.push_back(detail::ArrayRep{0, 0, 0});
    if (capacity > 0) { growArray(arrays_[index], capacity); }
    return Value::array(index);
}

std::uint32_t Context::arrayCount(Value a) const noexcept {
    assert(a.kind() == Value::Kind::Array);
    return arrays_[a.index()].count;
}

Value Context::arrayAt(Value a, std::uint32_t index) const noexcept {
    assert(a.kind() == Value::Kind::Array);
    const detail::ArrayRep &rep = arrays_[a.index()];
    if (index >= rep.count) { return Value::null(); }
    return pool_[rep.start + index];
}

bool Context::arraySet(Value a, std::uint32_t index, Value v) noexcept {
    assert(a.kind() == Value::Kind::Array);
    detail::ArrayRep &rep = arrays_[a.index()];
    if (index >= rep.count) { return false; }
    pool_[rep.start + index] = v;
    return true;
}

void Context::arrayPush(Value a, Value v) {
    assert(a.kind() == Value::Kind::Array);
    detail::ArrayRep &rep = arrays_[a.index()];
    // v пришёл копией, поэтому переезд pool_ внутри growArray ему не страшен.
    growArray(rep, rep.count + 1);
    pool_[rep.start + rep.count] = v;
    rep.count += 1;
}

bool Context::arrayPop(Value a, Value *out) noexcept {
    assert(a.kind() == Value::Kind::Array);
    detail::ArrayRep &rep = arrays_[a.index()];
    if (rep.count == 0) { return false; }
    rep.count -= 1;
    if (out != nullptr) { *out = pool_[rep.start + rep.count]; }
    return true;
}

void Context::growObject(detail::ObjectRep &rep, std::uint32_t needed) {
    if (needed <= rep.capacity) { return; }

    std::uint32_t capacity = rep.capacity == 0 ? 4 : rep.capacity;
    while (capacity < needed) {
        assert(capacity <= 0x7fffffffu && "объект перерос uint32");
        capacity *= 2;
    }

    const std::uint32_t start = static_cast<std::uint32_t>(entries_.size());
    entries_.insert(entries_.end(), capacity, detail::Entry{0, 0, Value::null()});
    for (std::uint32_t i = 0; i < rep.count; ++i) {
        entries_[start + i] = entries_[rep.start + i];
    }

    rep.start = start;
    rep.capacity = capacity;
}

std::uint32_t Context::findKey(const detail::ObjectRep &rep, std::string_view key,
                               bool *found) const noexcept {
    // Пары отсортированы по ключу, поиск двоичный: на типичных 3–20 ключах
    // это дешевле хеш-таблицы и не выделяет ничего сверх самого массива.
    std::uint32_t low = 0;
    std::uint32_t high = rep.count;
    while (low < high) {
        const std::uint32_t mid = low + (high - low) / 2;
        const detail::Entry &entry = entries_[rep.start + mid];
        const std::string_view candidate = textAt(entry.keyOffset, entry.keyLength);
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

Value Context::makeObject(std::uint32_t capacity) {
    const std::uint32_t index = static_cast<std::uint32_t>(objects_.size());
    objects_.push_back(detail::ObjectRep{0, 0, 0});
    if (capacity > 0) { growObject(objects_[index], capacity); }
    return Value::object(index);
}

std::uint32_t Context::objectCount(Value o) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    return objects_[o.index()].count;
}

Value Context::objectGet(Value o, std::string_view key) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectRep &rep = objects_[o.index()];
    bool found = false;
    const std::uint32_t at = findKey(rep, key, &found);
    if (!found) { return Value::null(); }
    return entries_[rep.start + at].value;
}

bool Context::objectHas(Value o, std::string_view key) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    bool found = false;
    findKey(objects_[o.index()], key, &found);
    return found;
}

std::string_view Context::objectKeyAt(Value o, std::uint32_t i) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectRep &rep = objects_[o.index()];
    if (i >= rep.count) { return {}; }
    const detail::Entry &entry = entries_[rep.start + i];
    return textAt(entry.keyOffset, entry.keyLength);
}

Value Context::objectValueAt(Value o, std::uint32_t i) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectRep &rep = objects_[o.index()];
    if (i >= rep.count) { return Value::null(); }
    return entries_[rep.start + i].value;
}

void Context::objectSet(Value o, std::string_view key, Value v) {
    assert(o.kind() == Value::Kind::Object);
    detail::ObjectRep &rep = objects_[o.index()];

    bool found = false;
    const std::uint32_t at = findKey(rep, key, &found);
    if (found) {
        entries_[rep.start + at].value = v;
        return;
    }

    // appendText вправе переселить text_, поэтому смещение берётся до роста
    // entries_, а срез key после этой строки уже не трогаем.
    const std::uint32_t keyOffset = appendText(key);
    const std::uint32_t keyLength = static_cast<std::uint32_t>(key.size());

    growObject(rep, rep.count + 1);
    for (std::uint32_t i = rep.count; i > at; --i) {
        entries_[rep.start + i] = entries_[rep.start + i - 1];
    }
    entries_[rep.start + at] = detail::Entry{keyOffset, keyLength, v};
    rep.count += 1;
}

std::size_t Context::bytesUsed() const noexcept {
    return pool_.size() * sizeof(Value) +
           arrays_.size() * sizeof(detail::ArrayRep) +
           objects_.size() * sizeof(detail::ObjectRep) +
           entries_.size() * sizeof(detail::Entry) + text_.size();
}

std::size_t Context::bytesReserved() const noexcept {
    return pool_.capacity() * sizeof(Value) +
           arrays_.capacity() * sizeof(detail::ArrayRep) +
           objects_.capacity() * sizeof(detail::ObjectRep) +
           entries_.capacity() * sizeof(detail::Entry) + text_.capacity();
}

}  // namespace CS
