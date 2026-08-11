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
