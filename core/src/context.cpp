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

Context::Context() { roots_ = makeObject(); }
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
    // Граница строгая: непустой срез пула начинается строго внутри него, а пустой
    // источник уходит раньше, чем понадобится адрес. Включающая граница приняла бы
    // за алиас чужой буфер, оказавшийся вплотную за пулом, и скопировала бы нули.
    const bool aliases = first != nullptr && from >= base && from < base + text_.size();
    const std::size_t inner = aliases ? static_cast<std::size_t>(from - base) : 0;

    text_.resize(text_.size() + bytes.size());
    if (bytes.empty()) { return offset; }

    const char *source = aliases ? text_.data() + inner : bytes.data();
    std::memcpy(text_.data() + offset, source, bytes.size());
    return offset;
}

std::string_view Context::textAt(std::uint32_t offset,
                                 std::uint32_t length) const noexcept {
    // Проверяется пустота пула, а не длина: пустой ключ обязан отличаться от
    // отсутствующего, иначе chupa_object_key_at не сможет вернуть NULL только
    // за границей.
    if (text_.empty()) { return {}; }
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

// Парная функция — growObject: правку в одной надо повторять в другой.
void Context::growArray(detail::ArrayRep &rep, std::uint32_t needed, bool exact) {
    if (needed <= rep.capacity) { return; }

    // Точный размер — для вызывающего, который знает длину заранее: удвоение
    // ему только тратит память. Рост от push, наоборот, удваивает, чтобы не
    // переезжать на каждом элементе.
    std::uint32_t capacity = needed;
    if (!exact) {
        capacity = rep.capacity == 0 ? 4 : rep.capacity;
        while (capacity < needed) {
            assert(capacity <= 0x7fffffffu && "массив перерос uint32");
            capacity *= 2;
        }
    }

    // Новый диапазон дописывается в хвост, старый бросается мусором:
    // освобождения по одному нет (спека §5). Индекс заголовка при этом не
    // меняется — на нём стоит идентичность и все алиасы.
    assert(pool_.size() + capacity <= 0xffffffffu && "пул массивов перерос uint32");
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
    if (capacity > 0) { growArray(arrays_[index], capacity, /*exact=*/true); }
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
    // v пришёл копией, поэтому переезд pool_ внутри growArray ему не страшен.
    // Заголовок перечитывается после роста: под единой ареной (docs/backlog.md
    // B1) заголовки будут жить в той же памяти, что и данные, и ссылка,
    // взятая до роста, повиснет.
    growArray(arrays_[a.index()], arrays_[a.index()].count + 1);
    detail::ArrayRep &rep = arrays_[a.index()];
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

// Парная функция — growArray: правку в одной надо повторять в другой.
void Context::growObject(detail::ObjectRep &rep, std::uint32_t needed, bool exact) {
    if (needed <= rep.capacity) { return; }

    // Точный размер — для вызывающего, который знает длину заранее: удвоение
    // ему только тратит память. Рост от вставки, наоборот, удваивает, чтобы
    // не переезжать на каждой паре.
    std::uint32_t capacity = needed;
    if (!exact) {
        capacity = rep.capacity == 0 ? 4 : rep.capacity;
        while (capacity < needed) {
            assert(capacity <= 0x7fffffffu && "объект перерос uint32");
            capacity *= 2;
        }
    }

    assert(entries_.size() + capacity <= 0xffffffffu && "пул пар перерос uint32");
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
    if (capacity > 0) { growObject(objects_[index], capacity, /*exact=*/true); }
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

    bool found = false;
    const std::uint32_t at = findKey(objects_[o.index()], key, &found);
    if (found) {
        const detail::ObjectRep &rep = objects_[o.index()];
        entries_[rep.start + at].value = v;
        return;
    }

    // appendText вправе переселить text_, поэтому смещение берётся до роста
    // entries_, а срез key после этой строки уже не трогаем.
    const std::uint32_t keyOffset = appendText(key);
    const std::uint32_t keyLength = static_cast<std::uint32_t>(key.size());

    // Заголовок перечитывается после appendText и growObject: под единой
    // ареной (docs/backlog.md B1) заголовки будут жить в той же памяти, что
    // и данные, и ссылка, взятая до роста, повиснет.
    growObject(objects_[o.index()], objects_[o.index()].count + 1);
    detail::ObjectRep &rep = objects_[o.index()];
    for (std::uint32_t i = rep.count; i > at; --i) {
        entries_[rep.start + i] = entries_[rep.start + i - 1];
    }
    entries_[rep.start + at] = detail::Entry{keyOffset, keyLength, v};
    rep.count += 1;
}

Value Context::root(std::string_view name) const noexcept {
    return objectGet(roots_, name);
}

bool Context::hasRoot(std::string_view name) const noexcept {
    return objectHas(roots_, name);
}

void Context::setRoot(std::string_view name, Value v) { objectSet(roots_, name, v); }

std::uint32_t Context::rootCount() const noexcept { return objectCount(roots_); }

std::string_view Context::rootNameAt(std::uint32_t i) const noexcept {
    return objectKeyAt(roots_, i);
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
