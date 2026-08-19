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

/// Запись таблицы имён: имя и номер его ячейки в globalValues_.
///
/// Значения здесь нет намеренно — оно живёт в ячейке. Вставка нового имени
/// двигает эти записи, чтобы сохранить сортировку, и если бы значение лежало
/// тут, вместе с ним переехал бы и его адрес. Номер ячейки переезд переживает.
struct GlobalName {
    std::uint32_t nameOffset;  // индекс первого байта имени в text_
    std::uint32_t nameLength;
    GlobalSlot slot;
};

}  // namespace detail

Store::Store(Role role)
    : keys_(role == Role::Globals ? KeyTable::create() : nullptr),
      id_(g_nextStoreId.fetch_add(1, std::memory_order_relaxed)) {}

Store::~Store() {
    // На месте, а не в список: границы операции больше не будет, да и списка
    // здесь больше нет — он принадлежит выполнению, и то умирает раньше
    // (Context объявляет его после хранилища).
    for (Value v : globalValues_) { detail::releaseValue(v); }
    for (detail::StringBox *literal : literals_) { detail::release(literal); }
    // У арены операции таблицы нет вовсе — отпускать нечего.
    if (keys_ != nullptr) { KeyTable::release(keys_); }
}

std::uint32_t Store::appendText(std::string_view bytes) {
    const std::uint32_t offset = static_cast<std::uint32_t>(text_.size());
    assert(text_.size() + bytes.size() <= 0xffffffffu && "пул текста перерос uint32");

    // bytes вправе указывать внутрь text_ — так выглядит objectSet(o,
    // store.string(k), v). Рост пула переселит буфер, и указатель источника
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

std::string_view Store::textAt(std::uint32_t offset,
                                 std::uint32_t length) const noexcept {
    // Проверяется пустота пула, а не длина: пустой ключ обязан отличаться от
    // отсутствующего, иначе chupa_object_key_at не сможет вернуть NULL только
    // за границей.
    if (text_.empty()) { return {}; }
    return std::string_view(text_.data() + offset, length);
}

Value Store::makeString(std::string_view bytes) {
    const std::uint32_t offset = appendText(bytes);
    return Value::scratchString(offset, static_cast<std::uint32_t>(bytes.size()));
}

detail::StringBox *Store::internLiteral(std::string_view bytes) {
    detail::StringBox *box = detail::makeStringBox(bytes);
    literals_.push_back(box);
    return box;
}

std::string_view Store::string(Value v) const noexcept {
    assert(v.kind() == Value::Kind::String);
    // Два представления, один бит различения. Коробка самодостаточна и читается
    // без хранилища вовсе — метод остаётся методом только ради единообразия
    // вызова.
    if (v.region() == Value::Region::Scratch) { return textAt(v.index(), v.length()); }
    return static_cast<const detail::StringBox *>(v.box())->view();
}

std::uint32_t Store::beginString() noexcept {
    assert(build_.size() <= 0xffffffffu && "буфер сборки строки перерос uint32");
    return static_cast<std::uint32_t>(build_.size());
}

void Store::appendToString(std::string_view bytes) {
    build_.append(bytes);
}

Value Store::endString(std::uint32_t mark) noexcept {
    // makeString копирует из build_ в text_; алиас-проверка в appendText
    // сравнивает источник с диапазоном text_, а build_ — другое хранилище,
    // поэтому спутать их не может.
    const Value result = makeString(std::string_view(build_).substr(mark));
    build_.resize(mark);
    return result;
}

void Store::abortString(std::uint32_t mark) noexcept {
    build_.resize(mark);
}

void Store::clearSlow() noexcept {
    // Ёмкость при этом остаётся: std::vector::clear её не отдаёт, а
    // shrink_to_fit здесь не зовётся нигде — в этом весь смысл сброса.
    //
    // Агрегатов здесь нет и быть не может: они коробки, и живут они по счётчику,
    // а не по региону. Сбрасывать остаётся только байты.
    text_.clear();

    // Черновик сборки в норме уже пуст: format снимает за собой и на успехе
    // (endString), и на отказе (abortString). Очистка здесь — не уборка за
    // ним, а страховка на случай выхода посреди сборки.
    build_.clear();

    // Таблица имён не трогается: у временного региона она пуста всегда —
    // глобальные заводит только хост, а он пишет в постоянный.
}

std::uint32_t Store::findGlobal(std::string_view name,
                                bool *found) const noexcept {
    // Тот же двоичный поиск, что и findEntry, но по своему массиву. Ходят сюда
    // только компиляция и запись — на вычислении имя больше не разрешается.
    std::uint32_t low = 0;
    std::uint32_t high = static_cast<std::uint32_t>(globalNames_.size());
    while (low < high) {
        const std::uint32_t mid = low + (high - low) / 2;
        const detail::GlobalName &entry = globalNames_[mid];
        const std::string_view candidate = textAt(entry.nameOffset, entry.nameLength);
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
    return found ? globalNames_[at].slot : kNoGlobalSlot;
}

Value Store::globalValueAt(GlobalSlot slot) const noexcept {
    // Чужой номер сюда попасть не может иначе как через выражение, вычисляемое
    // на не своём контексте, — а это нарушение контракта (chupascript.h).
    assert(slot < globalValues_.size() && "номер ячейки выдан другим хранилищем");
    return globalValues_[slot];
}

Value Store::global(std::string_view name) const noexcept {
    const GlobalSlot slot = globalSlot(name);
    if (slot == kNoGlobalSlot) { return Value::null(); }
    return globalValues_[slot];
}

bool Store::hasGlobal(std::string_view name) const noexcept {
    bool found = false;
    findGlobal(name, &found);
    return found;
}

void Store::setGlobal(std::string_view name, Value v, Deferred &dead) {
    assert(detail::materialized(v) && "строка временного региона не материализована");

    bool found = false;
    const std::uint32_t at = findGlobal(name, &found);
    detail::retainValue(v);
    if (found) {
        // Ячейка глобальной переменной — корень: прежнее значение она
        // отпускает, и без этого повторное присваивание растило бы память
        // вечно.
        Value &slot = globalValues_[globalNames_[at].slot];
        dead.take(slot);
        slot = v;
        return;
    }

    // Длина снимается до appendText: тот вправе переселить text_, и хотя сам
    // срез длину переживает, порядок здесь тот же, что в objectSet, — после
    // этой строки name не трогаем.
    const std::uint32_t nameLength = static_cast<std::uint32_t>(name.size());
    const std::uint32_t nameOffset = appendText(name);

    // Ячейка дописывается в конец, и её номер — прежний размер. Место в
    // globalNames_ найдено до appendText и осталось верным: тот в таблицу имён
    // не пишет.
    const GlobalSlot slot = static_cast<GlobalSlot>(globalValues_.size());
    globalValues_.push_back(v);
    globalNames_.insert(globalNames_.begin() + at,
                        detail::GlobalName{nameOffset, nameLength, slot});
}

std::uint32_t Store::globalCount() const noexcept {
    return static_cast<std::uint32_t>(globalNames_.size());
}

std::string_view Store::globalNameAt(std::uint32_t i) const noexcept {
    if (i >= globalNames_.size()) { return {}; }
    const detail::GlobalName &entry = globalNames_[i];
    return textAt(entry.nameOffset, entry.nameLength);
}

std::size_t Store::bytesUsed() const noexcept {
    // Память коробок сюда не входит и войти не может: хранилище ею не владеет.
    // Считать живые коробки умеет detail::liveBoxCount (core/src/box.hpp).
    return text_.size() + globalNames_.size() * sizeof(detail::GlobalName) +
           globalValues_.size() * sizeof(Value);
}

std::size_t Store::bytesReserved() const noexcept {
    return text_.capacity() + build_.capacity() +
           globalNames_.capacity() * sizeof(detail::GlobalName) +
           globalValues_.capacity() * sizeof(Value);
}

}  // namespace CS
