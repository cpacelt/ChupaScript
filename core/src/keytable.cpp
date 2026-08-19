#include "keytable.hpp"

#include <cassert>

namespace CS {

KeyTable *KeyTable::create() { return new KeyTable(); }

void KeyTable::retain(KeyTable *table) noexcept {
    assert(table != nullptr);
    ++table->rc_;
}

void KeyTable::release(KeyTable *table) noexcept {
    assert(table != nullptr && table->rc_ > 0);
    if (--table->rc_ == 0) { delete table; }
}

std::uint32_t KeyTable::place(std::string_view key, bool *found) const noexcept {
    std::uint32_t low = 0;
    std::uint32_t high = static_cast<std::uint32_t>(sorted_.size());
    while (low < high) {
        const std::uint32_t mid = low + (high - low) / 2;
        const Rec &rec = byId_[sorted_[mid]];
        const std::string_view candidate(text_.data() + rec.offset, rec.length);
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

std::uint32_t KeyTable::find(std::string_view key) const noexcept {
    bool found = false;
    const std::uint32_t at = place(key, &found);
    return found ? sorted_[at] : kNoKey;
}

std::uint32_t KeyTable::intern(std::string_view key) {
    bool found = false;
    const std::uint32_t at = place(key, &found);
    if (found) { return sorted_[at]; }

    assert(text_.size() + key.size() <= 0xffffffffu && "арена имён переросла uint32");
    const std::uint32_t offset = static_cast<std::uint32_t>(text_.size());
    // Срез key внутрь text_ сюда не приходит: имена берутся из исходника, из
    // узла литерала либо от хоста, но не из самой таблицы — читать её наружу
    // умеет только bytes(), а его результат в intern не возвращается никогда.
    text_.insert(text_.end(), key.begin(), key.end());

    const std::uint32_t id = static_cast<std::uint32_t>(byId_.size());
    byId_.push_back(Rec{offset, static_cast<std::uint32_t>(key.size())});
    sorted_.insert(sorted_.begin() + at, id);
    return id;
}

std::string_view KeyTable::bytes(std::uint32_t id) const noexcept {
    assert(id < byId_.size() && "номер ключа выдан другой таблицей");
    // Проверяется пустота арены, а не длина: пустой ключ — законный ключ, и
    // отличаться от отсутствующего он обязан.
    if (text_.empty()) { return {}; }
    const Rec &rec = byId_[id];
    return std::string_view(text_.data() + rec.offset, rec.length);
}

std::uint32_t KeyTable::count() const noexcept {
    return static_cast<std::uint32_t>(byId_.size());
}

}  // namespace CS
