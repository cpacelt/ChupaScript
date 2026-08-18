#pragma once
#include <cassert>
#include <cstdint>
#include <type_traits>

namespace CS {

class Store;

/// Номер ячейки значения глобальной переменной в хранилище.
///
/// Живёт здесь, а не в store.hpp, потому что нужен обеим сторонам: хранилище
/// номера выдаёт, дерево разбора их запоминает (Ast::globalValuesSlot), а
/// зависимость ast.hpp → store.hpp завела бы синтаксис в знание о значениях.
///
/// Номер годен, пока живо выдавшее его хранилище: ячейки только добавляются в
/// конец и никогда не переезжают и не переиспользуются — удаления глобальных
/// переменных в языке нет. Поэтому номер, разрешённый однажды на компиляции,
/// остаётся верным навсегда, сколько бы имён ни завели после.
using GlobalSlot = std::uint32_t;

/// Имени нет. Значением ячейки быть не может: столько их не бывает.
inline constexpr GlobalSlot kNoGlobalSlot = 0xffffffffu;

/// Значение ChupaScript. Шесть типов из docs/semantics.md §2.1.
///
/// Строки и агрегаты адресуются индексами в пулы Store: значение без своего
/// хранилища бессмысленно, разрешить индекс может только то хранилище, которое
/// его выдал. Поэтому создать строку, массив или объект способен лишь Store —
/// соответствующие фабрики закрыты.
///
/// Раскладка и обоснование:
/// docs/superpowers/specs/2026-08-11-chupascript-values-design.md §4.
class Value {
   public:
    /// Вид значения. Закрытый список из docs/semantics.md §2.1.
    enum class Kind : std::uint8_t { Null, Boolean, Number, String, Object, Array };

    /// Где живёт то, на что значение ссылается: постоянные данные хоста или
    /// временные значения текущей операции. Разбор — docs/backlog.md [B57].
    enum class Region : std::uint8_t { Persistent, Scratch };

    [[nodiscard]] static Value null() noexcept {
        Value v;
        v.kind_ = Kind::Null;
        return v;
    }

    [[nodiscard]] static Value boolean(bool value) noexcept {
        Value v;
        v.kind_ = Kind::Boolean;
        v.boolean_ = value;
        return v;
    }

    [[nodiscard]] static Value number(double value) noexcept {
        Value v;
        v.kind_ = Kind::Number;
        v.number_ = value;
        return v;
    }

    [[nodiscard]] Kind kind() const noexcept { return kind_; }

    /// Осмыслен только у String, Object и Array — см. поле region_.
    [[nodiscard]] Region region() const noexcept { return region_; }

    /// Предусловие: kind() == Kind::Boolean.
    [[nodiscard]] bool booleanValue() const noexcept {
        assert(kind_ == Kind::Boolean);
        return boolean_;
    }

    /// Предусловие: kind() == Kind::Number.
    [[nodiscard]] double numberValue() const noexcept {
        assert(kind_ == Kind::Number);
        return number_;
    }

    /// Один ли это агрегат — сравнивает вид и индекс заголовка.
    ///
    /// У скаляров идентичности нет (docs/semantics.md §5.4), для них всегда
    /// false, в том числе при сравнении значения с самим собой.
    [[nodiscard]] bool sameAggregate(Value other) const noexcept {
        if (kind_ != other.kind_) { return false; }
        if (kind_ != Kind::Array && kind_ != Kind::Object) { return false; }
        return index_ == other.index_;
    }

   private:
    friend class Store;

    /// Индексы полны как тип, поэтому без закрытых фабрик любой код собрал бы
    /// значение-агрегат с произвольным номером заголовка. Закрываем доступом.
    ///
    /// Регион обязателен и без значения по умолчанию: значения, ссылающегося
    /// в никуда, не бывает, и забыть его проставить не должно компилироваться.
    [[nodiscard]] static Value string(std::uint32_t offset, std::uint32_t length,
                                      Region region) noexcept {
        Value v;
        v.kind_ = Kind::String;
        v.length_ = length;
        v.index_ = offset;
        v.region_ = region;
        return v;
    }

    [[nodiscard]] static Value array(std::uint32_t index, Region region) noexcept {
        Value v;
        v.kind_ = Kind::Array;
        v.index_ = index;
        v.region_ = region;
        return v;
    }

    [[nodiscard]] static Value object(std::uint32_t index, Region region) noexcept {
        Value v;
        v.kind_ = Kind::Object;
        v.index_ = index;
        v.region_ = region;
        return v;
    }

    [[nodiscard]] std::uint32_t index() const noexcept {
        assert(kind_ == Kind::String || kind_ == Kind::Array || kind_ == Kind::Object);
        return index_;
    }

    [[nodiscard]] std::uint32_t length() const noexcept {
        assert(kind_ == Kind::String);
        return length_;
    }

    Value() noexcept : kind_(Kind::Null), length_(0), number_(0.0) {}

    Kind kind_;  // смещение 0
    // Смещение 1: байт был набивкой перед length_, поэтому регион достался
    // даром. У скаляров региона нет — они ничего не адресуют; поле у них не
    // читается, проверки региона касаются только String, Object и Array.
    Region region_ = Region::Persistent;
    std::uint32_t length_;  // смещение 4 — длина строки в байтах
    // TODO(B2): восемь байт вместо шестнадцати достижимы только через
    // NaN-boxing: double в объединении задаёт и размер, и выравнивание.
    union {  // смещение 8
        bool boolean_;
        double number_;
        std::uint32_t index_;
    };
};

static_assert(sizeof(Value) == 16, "Value должен оставаться в 16 байтах");
static_assert(std::is_trivially_copyable_v<Value>,
              "диапазоны значений копируются в пулах целиком");

}  // namespace CS
