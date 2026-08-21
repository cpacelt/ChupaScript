#include "store.hpp"

#include "box.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>

namespace CS {

namespace {

/// Source of Store ids. Atomic because two threads may each create a Context
/// (chupascript.h, threading contract), and the ids they get must differ.
/// Starts at 1 so that a zero-initialised id in a compiled unit never matches
/// a real Store.
std::atomic<std::uint32_t> g_nextStoreId{1};

}  // namespace

namespace detail {

/// Запись таблицы имён: имя и номер его ячейки в values_.
///
/// Значения здесь нет намеренно — оно живёт в ячейке. Вставка нового имени
/// двигает эти записи, чтобы сохранить сортировку, и если бы значение лежало
/// тут, вместе с ним переехал бы и его адрес. Номер ячейки переезд переживает.
struct GlobalName {
    std::uint32_t nameOffset;  // индекс первого байта имени в names_
    std::uint32_t nameLength;
    GlobalSlot slot;
};

}  // namespace detail

Store::Store() : keys_(KeyTable::create()), id_(g_nextStoreId.fetch_add(1, std::memory_order_relaxed)) {}

Store::~Store() {
    // На месте, а не в список: границы операции больше не будет, да и списка
    // здесь больше нет — он принадлежит выполнению, и то умирает раньше
    // (Context объявляет его после хранилища).
    for (Value v : values_) { detail::releaseValue(v); }
    if (keys_ != nullptr) { KeyTable::release(keys_); }
}

std::uint32_t Store::appendName(std::string_view bytes) {
    const std::uint32_t offset = static_cast<std::uint32_t>(names_.size());
    // Пул такого размера — другая проблема, и здесь срабатывает отладочное
    // утверждение, которое компилируется прочь под NDEBUG.
    assert(names_.size() + bytes.size() <= 0xffffffffu && "пул имён перерос uint32");

    // bytes may point back into names_ itself — that is what
    // Store.AcceptsItsOwnNameSliceBack exercises: a name sliced out of this
    // same Store handed straight back to setGlobal. Growing the vector may
    // relocate its buffer, and a raw pointer into the source would then
    // dangle mid-copy, so the source's position is remembered as an offset,
    // not an address.
    const char *first = names_.data();
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(first);
    const std::uintptr_t from = reinterpret_cast<std::uintptr_t>(bytes.data());
    // Strict bound: a non-empty slice of the pool starts strictly inside it,
    // and an empty source never reaches the address check below. A
    // non-strict bound would mistake an unrelated buffer sitting right past
    // the pool's end for an alias and copy zeros from it.
    const bool aliases = first != nullptr && from >= base && from < base + names_.size();
    const std::size_t inner = aliases ? static_cast<std::size_t>(from - base) : 0;

    names_.resize(names_.size() + bytes.size());
    if (bytes.empty()) { return offset; }

    const char *source = aliases ? names_.data() + inner : bytes.data();
    std::memcpy(names_.data() + offset, source, bytes.size());
    return offset;
}

std::string_view Store::nameAt(std::uint32_t offset,
                                std::uint32_t length) const noexcept {
    // Проверяется пустота пула, а не длина: пустой ключ обязан отличаться от
    // отсутствующего, иначе chupa_object_key_at не сможет вернуть NULL только
    // за границей.
    if (names_.empty()) { return {}; }
    return std::string_view(names_.data() + offset, length);
}

std::uint32_t Store::findGlobal(std::string_view name,
                                bool *found) const noexcept {
    // Тот же двоичный поиск, что и findEntry, но по своему массиву. Ходят сюда
    // только компиляция и запись — на вычислении имя больше не разрешается.
    std::uint32_t low = 0;
    std::uint32_t high = static_cast<std::uint32_t>(slots_.size());
    while (low < high) {
        const std::uint32_t mid = low + (high - low) / 2;
        const detail::GlobalName &entry = slots_[mid];
        const std::string_view candidate = nameAt(entry.nameOffset, entry.nameLength);
        if (candidate < name) {
            low = mid + 1;
        } else if (name < candidate) {
            high = mid;
        } else {
            *found = true;
            return mid;
        }
    }
    *found = false;
    return low;
}

GlobalSlot Store::globalSlot(std::string_view name) const noexcept {
    bool found = false;
    const std::uint32_t at = findGlobal(name, &found);
    return found ? slots_[at].slot : kNoGlobalSlot;
}

Value Store::globalValueAt(GlobalSlot slot) const noexcept {
    // Чужой номер сюда попасть не может иначе как через выражение, вычисляемое
    // на не своём контексте, — а это нарушение контракта (chupascript.h).
    assert(slot < values_.size() && "номер ячейки выдан другим хранилищем");
    return values_[slot];
}

Value Store::global(std::string_view name) const noexcept {
    const GlobalSlot slot = globalSlot(name);
    if (slot == kNoGlobalSlot) { return Value::null(); }
    return values_[slot];
}

bool Store::hasGlobal(std::string_view name) const noexcept {
    bool found = false;
    findGlobal(name, &found);
    return found;
}

void Store::setGlobal(std::string_view name, Value v, Deferred &dead) {
    bool found = false;
    const std::uint32_t at = findGlobal(name, &found);
    detail::retainValue(v);
    if (found) {
        // Ячейка глобальной переменной — корень: прежнее значение она
        // отпускает, и без этого повторное присваивание растило бы память
        // вечно.
        Value &slot = values_[slots_[at].slot];
        dead.take(slot);
        slot = v;
        // Эпоха поднимается здесь, а не у вызывающего: запись без подъёма даёт
        // не «медленно», а молча застывший экран (спека §4.5), поэтому она
        // обязана быть невыразима мимо этой строки.
        epochs_.bump(slots_[at].slot, clock_.tick());
        return;
    }

    // Длина снимается до appendName: тот вправе переселить names_, и хотя сам
    // срез длину переживает, порядок здесь тот же, что в objectSet, — после
    // этой строки name не трогаем.
    const std::uint32_t nameLength = static_cast<std::uint32_t>(name.size());
    const std::uint32_t nameOffset = appendName(name);

    // Ячейка дописывается в конец, и её номер — прежний размер. Место в
    // slots_ найдено до appendName и осталось верным: тот в таблицу имён
    // не пишет.
    const GlobalSlot slot = static_cast<GlobalSlot>(values_.size());
    values_.push_back(v);
    // Рождение ячейки берёт номер из той же ленты, что и мутация: номер,
    // выданный позже, строго больше выданного раньше, и на этом стоит §4.4.
    epochs_.open(slot, clock_.tick());
    slots_.insert(slots_.begin() + at,
                  detail::GlobalName{nameOffset, nameLength, slot});
}

void Store::setGlobalAt(GlobalSlot slot, Value v, Deferred &dead) {
    assert(slot < values_.size() && "номер ячейки выдан другим хранилищем");
    // retain нового идёт первым: при записи значения в самоё себя порядок
    // наоборот уронил бы счётчик в ноль между отпусканием и присваиванием.
    detail::retainValue(v);
    Value &cell = values_[slot];
    dead.take(cell);
    cell = v;
    epochs_.bump(slot, clock_.tick());
}

std::uint32_t Store::globalCount() const noexcept {
    return static_cast<std::uint32_t>(slots_.size());
}

std::string_view Store::globalNameAt(std::uint32_t i) const noexcept {
    if (i >= slots_.size()) { return {}; }
    const detail::GlobalName &entry = slots_[i];
    return nameAt(entry.nameOffset, entry.nameLength);
}

std::size_t Store::bytesUsed() const noexcept {
    // Память коробок сюда не входит и войти не может: хранилище ею не владеет.
    // Считать живые коробки умеет detail::liveBoxCount (core/src/box.hpp).
    return names_.size() + slots_.size() * sizeof(detail::GlobalName) +
           values_.size() * sizeof(Value);
}

std::size_t Store::bytesReserved() const noexcept {
    return names_.capacity() +
           slots_.capacity() * sizeof(detail::GlobalName) +
           values_.capacity() * sizeof(Value);
}

}  // namespace CS
