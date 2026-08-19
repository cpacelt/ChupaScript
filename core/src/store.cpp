#include "store.hpp"

#include "node.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace CS {

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

/// Отпустить значение на месте. Только для деструктора: там границы операции
/// уже не будет, и откладывать некуда.
static void releaseValueNow(Value v) noexcept {
    if (v.addressesStore() && v.region() == Value::Region::Counted) {
        detail::release(v.node());
    }
}

Store::Store(Value::Region region, KeyTable *keys)
    : region_(region), keys_(keys != nullptr ? keys : KeyTable::create()) {
    if (keys != nullptr) { KeyTable::retain(keys_); }
}

Store::~Store() {
    // Порядок важен: сначала ссылки, потом оснастка. Узел объекта отпускает
    // таблицу имён сам, и делать это надо, пока она ещё жива.
    drainPending();
    for (Value v : globalValues_) { releaseValueNow(v); }
    for (detail::StrNode *literal : literals_) { detail::release(literal); }
    KeyTable::release(keys_);
}

void Store::retainValue(Value v) noexcept {
    if (v.addressesStore() && v.region() == Value::Region::Counted) {
        detail::retain(v.node());
    }
}

void Store::defer(Value v) {
    if (v.addressesStore() && v.region() == Value::Region::Counted) {
        pending_.push_back(v.node());
    }
}

void Store::drainPending() noexcept {
    // Обход по индексу, а не range-for: освобождение узла в принципе может
    // дописать в pending_, и итератор вектора этого не переживёт.
    for (std::size_t i = 0; i < pending_.size(); ++i) {
        detail::release(pending_[i]);
    }
    pending_.clear();
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
    if (region_ == Value::Region::Scratch) {
        const std::uint32_t offset = appendText(bytes);
        return Value::string(offset, static_cast<std::uint32_t>(bytes.size()),
                             Value::Region::Scratch);
    }
    return materialize(bytes);
}

Value Store::materialize(std::string_view bytes) {
    detail::StrNode *node = detail::makeStrNode(bytes);
    pending_.push_back(node);   // ссылка создателя — до ближайшей границы
    return Value::string(node, node->len);
}

detail::StrNode *Store::internLiteral(std::string_view bytes) {
    detail::StrNode *node = detail::makeStrNode(bytes);
    literals_.push_back(node);
    return node;
}

std::string_view Store::string(Value v) const noexcept {
    assert(v.kind() == Value::Kind::String);
    // Два представления, один бит различения. Узел самодостаточен и читается
    // без хранилища вовсе — метод остаётся методом только ради единообразия
    // вызова.
    if (v.region() == Value::Region::Scratch) { return textAt(v.index(), v.length()); }
    return static_cast<const detail::StrNode *>(v.node())->view();
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

void Store::clear() noexcept {
    assert(region_ != Value::Region::Counted &&
           "постоянный регион не сбрасывается: на его значения ссылается хост");

    // Ёмкость при этом остаётся: std::vector::clear её не отдаёт, а
    // shrink_to_fit здесь не зовётся нигде — в этом весь смысл сброса.
    //
    // Агрегатов здесь нет и быть не может: они узлы, и живут они по счётчику,
    // а не по региону. Сбрасывать остаётся только байты.
    text_.clear();

    // Черновик сборки в норме уже пуст: format снимает за собой и на успехе
    // (endString), и на отказе (abortString). Очистка здесь — не уборка за
    // ним, а страховка на случай выхода посреди сборки.
    build_.clear();

    // Таблица имён не трогается: у временного региона она пуста всегда —
    // глобальные заводит только хост, а он пишет в постоянный.
}

Value Store::makeArray(std::uint32_t capacity) {
    detail::ArrayNode *node = detail::makeArrayNode(capacity);
    pending_.push_back(node);   // ссылка создателя — до ближайшей границы
    return Value::array(node);
}

std::uint32_t Store::arrayCount(Value a) const noexcept {
    assert(a.kind() == Value::Kind::Array);
    return static_cast<std::uint32_t>(
        static_cast<const detail::ArrayNode *>(a.node())->items.size());
}

Value Store::arrayAt(Value a, std::uint32_t index) const noexcept {
    assert(a.kind() == Value::Kind::Array);
    const detail::ArrayNode *node = static_cast<const detail::ArrayNode *>(a.node());
    if (index >= node->items.size()) { return Value::null(); }
    // Ссылка не берётся: значение живо, пока его держит сам массив, а массив
    // держит тот, кто его читает. Кадр скролла к счётчику не обращается.
    return node->items[index];
}

bool Store::arraySet(Value a, std::uint32_t index, Value v) noexcept {
    assert(a.kind() == Value::Kind::Array);
    assert(materialized(v) && "строка временного региона не материализована");
    detail::ArrayNode *node = static_cast<detail::ArrayNode *>(a.node());
    if (index >= node->items.size()) { return false; }
    retainValue(v);
    defer(node->items[index]);
    node->items[index] = v;
    return true;
}

void Store::arrayPush(Value a, Value v) {
    assert(a.kind() == Value::Kind::Array);
    assert(materialized(v) && "строка временного региона не материализована");
    // Дописывание в хвост, а не переезд диапазона в конец пула: массив теперь
    // владеет своими элементами сам.
    retainValue(v);
    static_cast<detail::ArrayNode *>(a.node())->items.push_back(v);
}

bool Store::arrayPop(Value a, Value *out) noexcept {
    assert(a.kind() == Value::Kind::Array);
    detail::ArrayNode *node = static_cast<detail::ArrayNode *>(a.node());
    if (node->items.empty()) { return false; }
    const Value last = node->items.back();
    node->items.pop_back();
    if (out != nullptr) { *out = last; }
    // Ссылка ячейки уходит в список, а не в delete: вызывающий читает снятое
    // значение сразу после возврата.
    defer(last);
    return true;
}

std::uint32_t Store::findKey(const detail::ObjectNode &node, std::string_view key,
                             bool *found) const noexcept {
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

Value Store::makeObject(std::uint32_t capacity) {
    detail::ObjectNode *node = detail::makeObjectNode(keys_, capacity);
    pending_.push_back(node);   // ссылка создателя — до ближайшей границы
    return Value::object(node);
}

std::uint32_t Store::objectCount(Value o) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    return static_cast<std::uint32_t>(
        static_cast<const detail::ObjectNode *>(o.node())->entries.size());
}

Value Store::objectGet(Value o, std::string_view key) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectNode &node = *static_cast<const detail::ObjectNode *>(o.node());
    bool found = false;
    const std::uint32_t at = findKey(node, key, &found);
    if (!found) { return Value::null(); }
    return node.entries[at].value;
}

bool Store::objectHas(Value o, std::string_view key) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    bool found = false;
    findKey(*static_cast<const detail::ObjectNode *>(o.node()), key, &found);
    return found;
}

std::string_view Store::objectKeyAt(Value o, std::uint32_t i) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectNode &node = *static_cast<const detail::ObjectNode *>(o.node());
    if (i >= node.entries.size()) { return {}; }
    return node.keys->bytes(node.entries[i].key);
}

Value Store::objectValueAt(Value o, std::uint32_t i) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectNode &node = *static_cast<const detail::ObjectNode *>(o.node());
    if (i >= node.entries.size()) { return Value::null(); }
    return node.entries[i].value;
}

void Store::objectSet(Value o, std::string_view key, Value v) {
    assert(o.kind() == Value::Kind::Object);
    assert(materialized(v) && "строка временного региона не материализована");
    detail::ObjectNode &node = *static_cast<detail::ObjectNode *>(o.node());

    bool found = false;
    const std::uint32_t at = findKey(node, key, &found);
    retainValue(v);
    if (found) {
        defer(node.entries[at].value);
        node.entries[at].value = v;
        return;
    }
    // Интернируется только тот ключ, который правда заводится: чтение
    // отсутствующего имени таблицу не засоряет — за этим следит findKey,
    // который сравнивает байты и в таблицу не пишет.
    node.entries.insert(node.entries.begin() + at,
                        detail::Entry{node.keys->intern(key), v});
}

std::uint32_t Store::findGlobal(std::string_view name,
                                bool *found) const noexcept {
    // Тот же двоичный поиск, что и findKey, но по своему массиву. Ходят сюда
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

void Store::setGlobal(std::string_view name, Value v) {
    assert(materialized(v) && "строка временного региона не материализована");

    bool found = false;
    const std::uint32_t at = findGlobal(name, &found);
    retainValue(v);
    if (found) {
        // Ячейка глобальной переменной — корень: прежнее значение она
        // отпускает, и без этого повторное присваивание растило бы память
        // вечно.
        Value &slot = globalValues_[globalNames_[at].slot];
        defer(slot);
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
    // Память узлов сюда не входит и войти не может: хранилище ею не владеет.
    // Считать живые узлы умеет detail::liveNodeCount (core/src/node.hpp).
    return text_.size() + globalNames_.size() * sizeof(detail::GlobalName) +
           globalValues_.size() * sizeof(Value);
}

std::size_t Store::bytesReserved() const noexcept {
    return text_.capacity() + build_.capacity() +
           globalNames_.capacity() * sizeof(detail::GlobalName) +
           globalValues_.capacity() * sizeof(Value);
}

}  // namespace CS
